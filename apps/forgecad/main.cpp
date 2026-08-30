// ============================================================
// ForgeCAD 主程序
// ------------------------------------------------------------
// 结构（可视化 UI 版）：
//   MainWindow（从 mainwindow.ui 加载的可视化主窗口）
//     ├─ modelTree          左边：模型树（QTreeView）
//     ├─ viewportContainer  右边：3D 视图容器
//     │    └─ Viewport3D    我们自己写的 3D 视图控件（塞进容器里）
//     │         └─ OCCT 渲染的 Box 模型
//     └─ statusbar          底部状态栏
// ============================================================
#include <QApplication>     // Qt 应用外壳
#include <QVBoxLayout>      // 垂直布局（装 Viewport3D 用）
#include <QTimer>           // 延迟执行（等窗口显示后再放形状，防黑屏）
#include <QStatusBar>       // 状态栏

#include <spdlog/spdlog.h>  // 日志

#include "core/Version.h"          // 版本号
#include "geometry/ShapeFactory.h" // 建 Box 的工厂
#include "ui/Viewport3D.h"         // 3D 视图控件（自己写的）
#include "mainwindow.h"            // 主窗口类（mainwindow.ui 可视化界面）

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);   // 启动 Qt 应用

    // ---- 主窗口（从 .ui 加载的可视化界面）----
    MainWindow win;
    win.setWindowTitle(QString("ForgeCAD v%1")
        .arg(QString::fromStdString(forge::core::Version::string())));

    // ---- 把 Viewport3D 塞进 .ui 里的 viewportContainer ----
    // findChild 按名字找容器（不需要改 MainWindow 类），塞入 3D 视图。
    auto* container = win.findChild<QWidget*>("viewportContainer");
    forge::ui::Viewport3D* viewport = nullptr;
    if (container) {
        auto* vlayout = new QVBoxLayout(container);
        vlayout->setContentsMargins(0, 0, 0, 0);   // 不留边距
        viewport = new forge::ui::Viewport3D(container);
        vlayout->addWidget(viewport);              // 3D 视图占满整个容器
    } else {
        spdlog::error("main: viewportContainer not found in mainwindow.ui");
    }

    win.show();

    // ---- 建一个 Box 并显示到 3D 视图 ----
    QTimer::singleShot(0, [&win, viewport]() {
        auto box = forge::geometry::ShapeFactory::makeBox(100, 50, 30);
        if (!box.IsNull() && viewport) {
            viewport->showShape(box);
            win.statusBar()->showMessage("OCCT Box 已显示 ✔ 拖拽旋转 / 滚轮缩放");
            spdlog::info("ForgeCAD: Box displayed in 3D viewport");
        } else {
            win.statusBar()->showMessage("OCCT Box 创建失败 ✘");
            spdlog::error("ForgeCAD: Box creation failed");
        }
    });

    return app.exec();      // 进入事件循环
}
