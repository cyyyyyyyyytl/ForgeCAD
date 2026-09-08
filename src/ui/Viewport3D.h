// ============================================================
// Viewport3D：3D 视图控件（把 OCCT 的 3D 渲染嵌进 Qt 窗口）
// ------------------------------------------------------------
// 大白话：这是一个"能显示 3D 模型的 Qt 控件"。
//   它自己不管怎么建模，只负责：
//     ① 创建 OCCT 的 3D 场景（Viewer + View + Context）
//     ② 提供一个 showShapes() 接口——别人把多个 TopoDS_Shape 丢进来，它们一起显示
//     ③ 处理鼠标：左键旋转、滚轮缩放（W4 再完善平移/选择）
// 为什么这么设计：UI 和几何解耦——
//   Viewport3D 只认识"形状(Shape)"，不认识"Box/圆柱"这些业务概念。
//   （对应需求文档："GUI 与业务逻辑解耦"）
// ============================================================
#pragma once

// ---- Qt 部分 ----
#include <QWidget>                    // 基类：Qt 的"控件"（能放进窗口的东西）
#include <QTimer>                     // 定时器：持续刷新画面（OCCT 不会自己重绘）
#include <vector>                     // std::vector：一次接收并显示多个形状

// ---- OCCT 部分 ----
#include <AIS_InteractiveContext.hxx> // 场景管理器：管"显示哪些物体"
#include <V3d_View.hxx>               // 3D 视图：相机视角 + 画布
#include <V3d_Viewer.hxx>             // 3D 场景：容纳多个 View
#include <TopoDS_Shape.hxx>           // OCCT 形状类型（Box 等）
#include <AIS_Shape.hxx>              // OCCT 显示对象（形状的包装）


namespace forge::ui {

// QWidget 子类 = 一个可嵌入窗口的控件
class Viewport3D : public QWidget {
    Q_OBJECT                          // Qt 宏：启用信号槽等元对象功能（必须）

public:
    explicit Viewport3D(QWidget* parent = nullptr);   // 构造函数（父窗口参数）
    ~Viewport3D() override;                           // 析构：释放 OCCT 资源

    // 对外接口：把多个形状同时显示到 3D 视图里。
    // selectedIndex 表示哪个形状是当前选中项；-1 表示暂时不高亮任何形状。
    // 注意：这里接收的是“形状列表”，Viewport3D 不需要认识 Box/Cylinder 等业务类型。
    void showShapes(const std::vector<TopoDS_Shape>& shapes, int selectedIndex);

    // 视角：让模型"恰好装满"屏幕（W4 完善）
    void fitAll();

    // 演示用标准视角：轴测图适合观察三维形状，正视图适合查看正面轮廓。
    // 这两个函数只移动相机，不会修改 TopoDS_Shape 的真实几何数据。
    void setAxonometricView();
    void setFrontView();

protected:
    // 以下都是 QWidget 的"事件回调"，重写它们让 OCCT 视图跟随窗口变化
    void paintEvent(QPaintEvent*) override;    // 窗口需要重绘时被调用
    void resizeEvent(QResizeEvent*) override;  // 窗口大小变化时被调用
    void showEvent(QShowEvent*) override;      // 窗口首次显示时被调用
    void mousePressEvent(QMouseEvent*) override;   // 鼠标按下
    void mouseMoveEvent(QMouseEvent*) override;    // 鼠标移动
    void wheelEvent(QWheelEvent*) override;        // 滚轮

private:
    void initViewer();    // 初始化 OCCT 3D 场景（窗口显示后调一次）
    bool inited_ = false; // 防止 initViewer 被多次调用
    QTimer refreshTimer_; // 持续刷新：OCCT 画面会被覆盖，必须定时重绘

    // OCCT 的"句柄"（智能指针）——对象生命周期自动管理
    occ::handle<V3d_Viewer>             viewer_;    // 场景容器
    occ::handle<V3d_View>               view_;      // 视图（画布+相机）
    occ::handle<AIS_InteractiveContext> context_;   // 场景管理器（显示/隐藏物体）
    // 一个 TopoDS_Shape 需要包装成一个 AIS_Shape 才能显示。
    // 以前这里只保存一个 displayed_，每次显示新形状都会删掉旧形状；
    // 现在改成 vector，场景里就可以同时保留多个显示对象。
    std::vector<occ::handle<AIS_Shape>> displayedShapes_;
};

} // namespace forge::ui
