#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <QStatusBar>

#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "geometry/ShapeFactory.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QMainWindow win;
    win.setWindowTitle(QString("ForgeCAD v%1").arg(QString::fromStdString(forge::core::Version::string())));

    auto* central = new QWidget(&win);
    auto* layout = new QVBoxLayout(central);
    auto* label = new QLabel(QString("ForgeCAD 启动成功 (ForgeCAD %1)\n构建了一个 OCCT Box 模型。"),
                             central);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
    win.setCentralWidget(central);
    win.resize(640, 400);
    win.statusBar()->showMessage("就绪");

    // 验证 OCCT + spdlog 集成
    auto box = forge::geometry::ShapeFactory::makeBox(100, 50, 30);
    if (!box.IsNull()) {
        spdlog::info("ForgeCAD main: OCCT box created, not null.");
        win.statusBar()->showMessage("OCCT Box 已创建 ✔");
    } else {
        spdlog::error("ForgeCAD main: OCCT box creation failed!");
        win.statusBar()->showMessage("OCCT Box 创建失败 ✘");
    }

    win.show();
    return app.exec();
}
