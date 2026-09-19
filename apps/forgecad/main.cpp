// ============================================================
// ForgeCAD 主程序(装配层)
// ------------------------------------------------------------
// 只负责三件事：建 Qt 应用 → 建主窗口 → 进事件循环。
// 模型和三维视图由 MainWindow 协调；main 不关心具体建模逻辑。
// ============================================================
#include <QApplication>      // Qt 应用外壳：管理事件循环、平台插件和全局资源。
#include "core/Version.h"   // 从 CMake 注入信息取得唯一版本号。
#include "mainwindow.h"     // ForgeCAD 主窗口类声明。

// 操作系统从这里进入 ForgeCAD；argc/argv 原样交给 Qt 解析平台参数。
int main(int argc, char* argv[])
{
    // QApplication 必须先于任何 QWidget 创建，并在整个 GUI 生命周期内存活。
    QApplication app(argc, argv);

    // 主窗口是栈对象；事件循环结束后会自动析构并释放其 Qt 子对象和文档。
    MainWindow win;
    // 标题中的版本来自统一 Version 接口，避免界面硬编码与构建版本不一致。
    win.setWindowTitle(QString("ForgeCAD v%1")
        .arg(QString::fromStdString(forge::core::Version::string())));
    win.show(); // QWidget 默认隐藏，显式 show 后才交给窗口系统显示。

    // exec 持续分发鼠标、键盘、绘制和网络信号；关闭最后窗口后返回退出码。
    return app.exec();
}
