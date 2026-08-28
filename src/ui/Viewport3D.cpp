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
        view_->SetBackgroundColor(Quantity_NOC_GRAY30);
        diagLog("[3] view created");

        // ④ 创建 Context（场景管理器）
        context_ = new AIS_InteractiveContext(viewer_);
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
// 显示一个形状
// ============================================================
void Viewport3D::showShape(const TopoDS_Shape& shape) {
    if (shape.IsNull()) {
        diagLog("showShape: shape is null!");
        spdlog::error("Viewport3D::showShape: shape is null");
        return;
    }
    diagLog("showShape: got valid shape");
    if (!context_) { diagLog("showShape: context_ is NULL!"); return; }
    if (!view_)    { diagLog("showShape: view_ is NULL!"); return; }

    auto aisShape = new AIS_Shape(shape);
    // OCCT 8.0: Display(对象, 显示模式, 选择模式, 是否刷新视图)
    context_->Display(aisShape, AIS_Shaded, 0, false);
    diagLog("Display called");
    view_->FitAll();
    view_->Redraw();
    diagLog("FitAll + Redraw done");
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

// 鼠标按下：开始旋转
void Viewport3D::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        view_->StartRotation(e->x(), e->y());
    }
}

// 鼠标移动：执行旋转
void Viewport3D::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) {
        view_->Rotation(e->x(), e->y());
        view_->Redraw();
    }
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
