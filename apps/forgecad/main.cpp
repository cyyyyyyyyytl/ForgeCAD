#include <QApplication>
#include <QMainWindow>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QTimer>

#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "geometry/ShapeFactory.h"
#include "ui/Viewport3D.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QMainWindow win;
    win.setWindowTitle(QString("ForgeCAD v%1").arg(QString::fromStdString(forge::core::Version::string())));
    win.resize(900, 600);

    auto* central = new QWidget(&win);
    auto* layout = new QVBoxLayout(central);
    auto* viewport = new forge::ui::Viewport3D(central);
    layout->addWidget(viewport, 1);
    win.setCentralWidget(central);
    win.statusBar()->showMessage("就绪");
    win.show();

    QTimer::singleShot(0, [&win, viewport]() {
        auto box = forge::geometry::ShapeFactory::makeBox(100, 50, 30);
        if (!box.IsNull()) {
            viewport->showShape(box);
            win.statusBar()->showMessage("OCCT Box 已显示");
        }
    });

    return app.exec();
}
