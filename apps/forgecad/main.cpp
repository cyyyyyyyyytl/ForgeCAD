// ============================================================
// ForgeCAD 主程序
// ------------------------------------------------------------
// 大白话：这是程序的"总装车间"——把各个零件拼成完整应用。
//   现在拼的是：
//     QMainWindow（主窗口，Qt 提供）
//       └─ Viewport3D（3D 视图控件，我们自己写的）
//            └─ OCCT 渲染的 Box 模型
// ============================================================
#include <QApplication>     // Qt 应用外壳（每个 Qt 程序必须有且只有一个）
#include <QMainWindow>      // 主窗口
#include <QStatusBar>       // 底部状态栏
#include <QLabel>           // 文本标签
#include <QVBoxLayout>      // 垂直布局（从上到下排控件）
#include <QWidget>          // 通用控件基类
#include <QTimer>           // 定时器（延迟执行，等窗口显示后再放形状）

#include <spdlog/spdlog.h>  // 日志

#include "core/Version.h"          // 版本号
#include "geometry/ShapeFactory.h" // 建 Box 的工厂
#include "ui/Viewport3D.h"         // 3D 视图控件（刚写的！）

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);   // 启动 Qt 应用

    // ---- 主窗口 ----
    QMainWindow win;
    win.setWindowTitle(QString("ForgeCAD v%1")
        .arg(QString::fromStdString(forge::core::Version::string())));
    win.resize(900, 600);           // 窗口大小（比之前大，3D 需要空间）

    // ---- 中央区域：垂直布局 ----
    auto* central = new QWidget(&win);
    auto* layout = new QVBoxLayout(central);

    // 顶部：一行说明文字
    auto* label = new QLabel("ForgeCAD 3D 视图（拖拽旋转，滚轮缩放）", central);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    // 中部：3D 视图控件（今天的主角！）
    auto* viewport = new forge::ui::Viewport3D(central);
    layout->addWidget(viewport, /*stretch=*/1);   // stretch=1：占满剩余空间

    win.setCentralWidget(central);
    win.statusBar()->showMessage("就绪");

    win.show();             // 显示窗口（QWidget 默认隐藏）

    // ---- 建一个 Box 并显示到 3D 视图 ----
    // 关键：必须等窗口【真正显示后】再放形状。
    //   原因：winId() 返回的原生窗口句柄在窗口显示前可能还没最终确定，
    //   过早绑定会让 OCCT 画到无效句柄上 → 黑屏。
    //   用 QTimer::singleShot(0, ...) 把放形状推迟到事件循环开始后。
    QTimer::singleShot(0, [&win, &viewport]() {
        auto box = forge::geometry::ShapeFactory::makeBox(100, 50, 30);
        if (!box.IsNull()) {
            viewport->showShape(box);               // 显示到 3D 视图！
            win.statusBar()->showMessage("OCCT Box 已显示 ✔ 拖拽旋转 / 滚轮缩放");
            spdlog::info("ForgeCAD: Box displayed in 3D viewport");
        } else {
            win.statusBar()->showMessage("OCCT Box 创建失败 ✘");
            spdlog::error("ForgeCAD: Box creation failed");
        }
    });

    return app.exec();      // 进入事件循环：程序在这里"转起来"，响应鼠标键盘
}
