// ============================================================
// ForgeCAD 主程序(装配层)
// ------------------------------------------------------------
// main.cpp 是"总装车间"：只负责把零件拼起来。
//   ① 创建 Qt 应用
//   ② 创建主窗口 MainWindow(内部已装好 3D 视图)
//   ③ 造一个 Box，通过接口显示
// 注意：这里不再 findChild —— 3D 视图由 MainWindow 内部创建，
//       外部只通过 showBox() 接口操作(封装)。
// ============================================================
#include <QApplication>     // Qt 应用外壳
#include <QTimer>           // 延迟执行(等窗口显示后再放形状，防黑屏)
#include <QStatusBar>       // 状态栏

#include <spdlog/spdlog.h>  // 日志

#include "core/Version.h"          // 版本号
#include "geometry/ShapeFactory.h" // 建 Box 的工厂
#include "mainwindow.h"            // 主窗口(内部有 3D 视图)

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);   // 启动 Qt 应用(必须有, 第一个)

    // ---- 创建主窗口(内部自动创建 3D 视图)----
    MainWindow win;
    win.setWindowTitle(QString("ForgeCAD v%1")
        .arg(QString::fromStdString(forge::core::Version::string())));
    win.show();

    // ---- 等窗口显示后, 造盒子并通过接口显示 ----
    // QTimer::singleShot(0, ...): 延迟到事件循环开始后执行
    //   (窗口显示后原生句柄才就绪, 否则 3D 视图黑屏)
    QTimer::singleShot(0, [&win]() {
        auto box = forge::geometry::ShapeFactory::makeBox(100, 50, 30);
        if (!box.IsNull()) {
            win.showBox(box);        // 通过接口显示(封装, 不碰内部)
            win.statusBar()->showMessage("OCCT Box 已显示 ✔ 拖拽旋转 / 滚轮缩放");
            spdlog::info("ForgeCAD: Box displayed in 3D viewport");
        } else {
            win.statusBar()->showMessage("OCCT Box 创建失败 ✘");
            spdlog::error("ForgeCAD: Box creation failed");
        }
    });

    return app.exec();      // 进入事件循环(程序在这里跑起来)
}
