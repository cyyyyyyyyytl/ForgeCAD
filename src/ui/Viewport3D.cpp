// ============================================================
// Viewport3D 实现：把 OCCT 3D 渲染嵌进 Qt 窗口
// ------------------------------------------------------------
// 关键知识（今天最重要的坑）：
//   早期方案用 WNT_Window(绑定 Windows 原生句柄 HWND)，但 Qt6 下
//   视图在 a.exec() 进入事件循环后会消失（OCCT 官方已知问题）。
//   正确做法：用 Aspect_NeutralWindow（"平台中立窗口"）——
//     窗口由 Qt 管理，OCCT 只负责"画"，
//     我们在 paintEvent 里调 view_->Redraw() 完成渲染。
//   四步曲不变：驱动 → Viewer → View → Context。
// ============================================================
#include "ui/Viewport3D.h"

// ---- Qt 事件相关 ----
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>                       // QTimer::singleShot：延迟执行（首次重绘用）

// ---- OCCT：图形驱动（OpenGL 后端）----
#include <OpenGl_GraphicDriver.hxx>       // OpenGL 图形驱动
#include <Aspect_DisplayConnection.hxx>   // 窗口系统连接（Windows 下基本是空的）
#include <Standard_Failure.hxx>           // OCCT 异常基类（抓初始化错误用）

// ---- OCCT：平台中立窗口（Qt6 集成关键，替代 WNT_Window）----
#include <Aspect_NeutralWindow.hxx>       // 中立窗口：窗口由 Qt 管，OCCT 只管画

// ---- OCCT：显示物体 ----
#include <AIS_Shape.hxx>                  // 把 TopoDS_Shape 包装成"可显示物体"
#include <AIS_DisplayMode.hxx>
#include <Aspect_GradientFillMethod.hxx>
#include <Aspect_TypeOfTriedronPosition.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Graphic3d_NameOfMaterial.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_TypeOfHighlight.hxx>
#include <Quantity_Color.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <V3d_TypeOfVisualization.hxx>

#include <spdlog/spdlog.h>                // 日志（记录关键步骤，方便排查）
#include <cstdio>                         // fprintf：写诊断日志到文件（一定能看到）

namespace forge::ui {

// 简易诊断日志：写到 C:\Users\15389\AppData\Local\Temp\forgecad_viewport.log
//   （qDebug 在 GUI 程序里看不到，用文件最可靠）
static void diagLog(const char* msg) {
    FILE* f = fopen("C:\\Users\\15389\\AppData\\Local\\Temp\\forgecad_viewport.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}
static void diagLog(const std::string& msg) { diagLog(msg.c_str()); }

// ============================================================
// 构造函数：只做 Qt 控件设置，【不】初始化 OCCT
// ============================================================
Viewport3D::Viewport3D(QWidget* parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    // WA_PaintOnScreen：OCCT 直接画屏幕，不走 Qt 缓冲（必须）
    setAttribute(Qt::WA_PaintOnScreen);
    // WA_NoSystemBackground：不擦背景，避免闪烁
    setAttribute(Qt::WA_NoSystemBackground);
    // 注意：不在构造函数里 initViewer()——
    //   此时控件还没显示，尺寸还是 0，OCCT 不知道画多大。
    //   必须等 showEvent（窗口真正显示、尺寸确定）后再初始化。

    // ---- 持续刷新（关键！）----
    // 为什么必须定时器？OCCT 的 NeutralWindow 模式没有自己的后台刷新，
    // 画面画一次后会被 Qt 覆盖掉（表现为"点一下才出现/又消失"）。
    // 官方 OcctQtViewer 的做法就是定时器驱动 Redraw。
    // 注意：直接调 view_->Redraw()，不走 update()/paintEvent，
    //   避免"Qt 擦背景 + OCCT 画"双重操作导致闪烁。
    refreshTimer_.setInterval(33);          // ~30fps，静态场景够流畅也不费 CPU
    connect(&refreshTimer_, &QTimer::timeout, this, [this]() {
        if (view_) view_->Redraw();
    });
    refreshTimer_.start();
}

// 析构：OCCT 对象用智能指针(handle)管理，自动释放（RAII）
Viewport3D::~Viewport3D() = default;

// ============================================================
// 初始化 3D 场景（四步曲 + 中立窗口）
// ============================================================
void Viewport3D::initViewer() {
    diagLog("=== initViewer start ===");
    try {
        // ① 创建 OpenGL 图形驱动
        auto driver = new OpenGl_GraphicDriver(new Aspect_DisplayConnection());
        diagLog("[1] driver created");

        // ② 创建 Viewer（场景容器）
        viewer_ = new V3d_Viewer(driver);
        viewer_->SetDefaultLights();
        viewer_->SetLightOn();
        diagLog("[2] viewer created");

        // ③ 创建 View（画布 + 相机）
        view_ = new V3d_View(viewer_);
        view_->SetBgGradientColors(
            Quantity_Color(0.18, 0.21, 0.25, Quantity_TOC_RGB),
            Quantity_Color(0.055, 0.065, 0.085, Quantity_TOC_RGB),
            Aspect_GradientFillMethod_Vertical,
            false);
        view_->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
        view_->SetProj(V3d_TypeOfOrientation_Zup_AxoRight);
        view_->ChangeRenderingParams().NbMsaaSamples = 4;
        diagLog("[3] view created");

        // ④ 创建 Context（场景管理器）
        context_ = new AIS_InteractiveContext(viewer_);
        context_->SetDisplayMode(AIS_Shaded, false);
        context_->SetPixelTolerance(4);
        context_->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic)->SetColor(
            Quantity_Color(0.25, 0.78, 1.0, Quantity_TOC_RGB));
        context_->HighlightStyle(Prs3d_TypeOfHighlight_Selected)->SetColor(
            Quantity_Color(1.0, 0.58, 0.12, Quantity_TOC_RGB));
        diagLog("[4] context created");

        // 关键：创建"中立窗口"并告诉它当前控件尺寸。
        //   不用 WNT_Window 绑定 HWND，避免 Qt6 下视图消失的 bug。
        auto aWin = new Aspect_NeutralWindow();
        // 必须把 Qt 控件的原生句柄(HWND)告诉 OCCT——
        //   否则 NeutralWindow::NativeHandle() 返回 0，OCCT 不知道往哪画（空白！）
        //   winId() 返回 WId(=quintptr)，先转成 void* 再转 Aspect_Drawable
        auto hwnd = reinterpret_cast<void*>(winId());
        aWin->SetNativeHandle(reinterpret_cast<Aspect_Drawable>(hwnd));
        aWin->SetSize(width(), height());          // 窗口大小 = 控件大小
        view_->SetWindow(aWin);
        view_->TriedronDisplay(
            Aspect_TOTP_LEFT_LOWER,
            Quantity_NOC_WHITE,
            0.075,
            V3d_ZBUFFER);
        diagLog("[5] SetWindow(NeutralWindow+handle) done, hwnd="
                + std::to_string(reinterpret_cast<long long>(hwnd)));
        view_->MustBeResized();
        diagLog("[6] MustBeResized done");
        view_->Redraw();
        diagLog("[7] initViewer complete");
    } catch (const Standard_Failure& e) {
        const char* m = e.GetMessageString();
        diagLog(std::string("!!! OCCT init FAILED: ") + (m ? m : "unknown"));
    } catch (...) {
        diagLog("!!! OCCT init FAILED (unknown exception)");
    }
}

// ============================================================
// 同时显示多个形状，并高亮当前选中的一个
// ============================================================
void Viewport3D::showShapes(const std::vector<TopoDS_Shape>& shapes, int selectedIndex) {
    if (!context_) { diagLog("showShapes: context_ is NULL!"); return; }
    if (!view_)    { diagLog("showShapes: view_ is NULL!"); return; }

    // ① 清掉上一轮的选中状态和显示对象。
    // false 表示先不立即刷新；全部处理完成后只统一 Redraw 一次，避免闪烁。
    context_->ClearSelected(false);
    for (const auto& displayed : displayedShapes_) {
        if (!displayed.IsNull()) {
            context_->Remove(displayed, false);
        }
    }
    displayedShapes_.clear();
    displayedShapes_.reserve(shapes.size());

    // ② 每个 TopoDS_Shape 都包装成自己的 AIS_Shape，并放进 3D 场景。
    // 空形状不显示；正常情况下 MainWindow 已经提前过滤掉空形状，
    // 这里再检查一次，是显示层自己的最后一道防御。
    for (const auto& shape : shapes) {
        if (shape.IsNull()) {
            spdlog::warn("Viewport3D::showShapes skipped a null shape");
            continue;
        }

        occ::handle<AIS_Shape> displayed = new AIS_Shape(shape);
        displayed->SetMaterial(
            Graphic3d_MaterialAspect(Graphic3d_NameOfMaterial_Satin));
        displayed->SetColor(
            Quantity_Color(0.72, 0.78, 0.86, Quantity_TOC_RGB));
        displayed->Attributes()->SetFaceBoundaryDraw(true);
        displayed->Attributes()->SetupOwnFaceBoundaryAspect();
        displayed->Attributes()->FaceBoundaryAspect()->SetColor(
            Quantity_Color(0.12, 0.15, 0.19, Quantity_TOC_RGB));
        displayed->Attributes()->FaceBoundaryAspect()->SetWidth(1.25);

        // OCCT 8.0: Display(对象, 显示模式, 选择模式, 是否刷新视图)
        context_->Display(displayed, AIS_Shaded, 0, false);
        displayedShapes_.push_back(displayed);
    }

    // ③ selectedIndex 是“有效形状列表”的下标。
    setSelectedIndex(selectedIndex);

    diagLog("showShapes: displayed " + std::to_string(displayedShapes_.size()) + " shapes");
    view_->FitAll();
    view_->Redraw();
    diagLog("FitAll + Redraw done");
}

void Viewport3D::setSelectedIndex(int selectedIndex)
{
    if (!context_) return;

    context_->ClearSelected(false);
    if (selectedIndex >= 0
        && selectedIndex < static_cast<int>(displayedShapes_.size())) {
        context_->SetSelected(displayedShapes_[selectedIndex], false);
    }
    if (view_) view_->Redraw();
}

// 调整视角到整个模型
void Viewport3D::fitAll() {
    if (view_) { view_->FitAll(); view_->Redraw(); }
}

// ============================================================
// Qt 事件回调：让 OCCT 视图跟随 Qt 窗口
// ============================================================

// 窗口【真正显示后】才初始化 OCCT（此时尺寸已确定）
void Viewport3D::showEvent(QShowEvent*) {
    if (!inited_) {
        inited_ = true;
        initViewer();
    }
}

// paintEvent 留空：重绘完全由 refreshTimer_ 驱动（直接调 view_->Redraw()）。
//   不在 paintEvent 里画，避免"Qt 擦背景→OCCT 画"的双重操作造成闪烁。
void Viewport3D::paintEvent(QPaintEvent*) {
    // 故意留空（定时器会负责重绘）
}

// 窗口大小变化时 → 更新 OCCT 视图尺寸
void Viewport3D::resizeEvent(QResizeEvent*) {
    if (view_) {
        view_->MustBeResized();   // 通知 OCCT 尺寸变了（内部会重新分配画布）
    }
}

// 左键选择；中键拖动旋转；Shift+中键拖动平移。
void Viewport3D::mousePressEvent(QMouseEvent* e) {
    pressPosition_ = e->position().toPoint();
    lastMousePosition_ = pressPosition_;

    if (e->button() == Qt::MiddleButton && view_) {
        panning_ = e->modifiers().testFlag(Qt::ShiftModifier);
        rotating_ = !panning_;
        if (rotating_) view_->StartRotation(e->x(), e->y());
        e->accept();
    } else if (e->button() == Qt::LeftButton) {
        e->accept();
    }
}

void Viewport3D::mouseMoveEvent(QMouseEvent* e) {
    if (rotating_ && (e->buttons() & Qt::MiddleButton) && view_) {
        view_->Rotation(e->x(), e->y());
        view_->Redraw();
        lastMousePosition_ = e->position().toPoint();
        return;
    }

    if (panning_ && (e->buttons() & Qt::MiddleButton) && view_) {
        const QPoint current = e->position().toPoint();
        const QPoint delta = current - lastMousePosition_;
        view_->Pan(delta.x(), -delta.y());
        view_->Redraw();
        lastMousePosition_ = current;
        return;
    }

    if (e->buttons() == Qt::NoButton && context_ && view_) {
        context_->MoveTo(e->x(), e->y(), view_, true);
    }
}

void Viewport3D::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton) {
        rotating_ = false;
        panning_ = false;
        e->accept();
        return;
    }

    if (e->button() != Qt::LeftButton || !context_ || !view_) return;
    if ((e->position().toPoint() - pressPosition_).manhattanLength() > 4) return;

    context_->MoveTo(e->x(), e->y(), view_, true);
    if (!context_->HasDetected()) {
        context_->ClearSelected(false);
        view_->Redraw();
        emit shapeSelected(-1);
        return;
    }

    const occ::handle<AIS_InteractiveObject> detected = context_->DetectedInteractive();
    int selectedIndex = -1;
    for (int i = 0; i < static_cast<int>(displayedShapes_.size()); ++i) {
        if (displayedShapes_[i].get() == detected.get()) {
            selectedIndex = i;
            break;
        }
    }

    context_->SelectDetected(AIS_SelectionScheme_Replace);
    view_->Redraw();
    emit shapeSelected(selectedIndex);
}

void Viewport3D::leaveEvent(QEvent*)
{
    if (context_) context_->ClearDetected(true);
}

// 滚轮：以【鼠标光标位置】为中心缩放（专业 CAD 的手感）
//   OCCT 8 的机制：
//     StartZoomAtPoint(cx, cy) —— 缩放中心 = 光标位置
//     ZoomAtPoint(x1,y1, x2,y2) —— 按"起点→终点"的距离缩放：
//       * 缩放中心 = StartZoomAtPoint 设的点（即光标）
//       * 起点到终点的距离 = 缩放量（距离大→放大，距离小→缩小）
//   ⚠ 不要再用 SetZoom()：它以【屏幕中心】缩放，会把光标中心覆盖掉！
//   每格滚轮 = 终点偏移 50px = 缩放 20%
void Viewport3D::wheelEvent(QWheelEvent* e) {
    if (!view_) return;
    const int x = e->position().x();   // 光标在当前控件内的 X
    const int y = e->position().y();   // 光标在当前控件内的 Y

    view_->StartZoomAtPoint(x, y);     // ① 缩放中心 = 光标

    // ② 终点偏移：向上滚(+120) → 终点右移 50px → 距离大 → 放大
    //    向下滚(-120) → 终点左移 → 距离小 → 缩小
    const double wheel = e->angleDelta().y() / 120.0;         // 每格 = ±1
    const int offset = static_cast<int>(wheel * 50.0);        // 每格偏移 50px
    view_->ZoomAtPoint(x, y, x + offset, y);                  // 以光标为中心缩放
    view_->Redraw();
}

} // namespace forge::ui
