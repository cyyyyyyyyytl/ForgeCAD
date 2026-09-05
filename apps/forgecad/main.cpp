// ============================================================
// ForgeCAD 主程序(装配层)
// ------------------------------------------------------------
// 只负责三件事：建 Qt 应用 → 建主窗口 → 进事件循环。
// 盒子/模型已由 MainWindow 内部管理(面板改参数 → BoxFeature 重建)，
// 这里不再直接调 ShapeFactory——main 不关心"模型怎么来"。
// ============================================================
#include <QApplication>     // Qt 应用外壳
#include "core/Version.h"   // 版本号
#include "mainwindow.h"     // 主窗口(内部管理 3D 视图 + 参数化盒子)

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);   // 启动 Qt 应用(必须有, 第一个)

    MainWindow win;                 // 创建主窗口(内部自动创建 3D 视图和 BoxFeature)
    win.setWindowTitle(QString("ForgeCAD v%1")
        .arg(QString::fromStdString(forge::core::Version::string())));
    win.show();

    return app.exec();              // 进入事件循环(程序在这里跑起来)
}