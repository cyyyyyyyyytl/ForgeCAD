// ============================================================
// ForgeCAD 主程序(装配层)
// ------------------------------------------------------------
// 只负责三件事：建 Qt 应用 → 建主窗口 → 进事件循环。
// 模型和三维视图由 MainWindow 协调；main 不关心具体建模逻辑。
// ============================================================
#include <QApplication>     // Qt 应用外壳
#include "core/Version.h"   // 版本号
#include "mainwindow.h"     // ForgeCAD 主窗口

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);   // 启动 Qt 应用(必须有, 第一个)

    MainWindow win;                 // 创建主窗口
    win.setWindowTitle(QString("ForgeCAD v%1")
        .arg(QString::fromStdString(forge::core::Version::string())));
    win.show();

    return app.exec();              // 进入事件循环(程序在这里跑起来)
}
