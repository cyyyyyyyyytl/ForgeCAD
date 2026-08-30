#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui/Viewport3D.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setViewport(forge::ui::Viewport3D* viewport)
{
    viewport_ = viewport;
}

void MainWindow::showBox(const TopoDS_Shape& shape)
{
    if (viewport_) {
        viewport_->showShape(shape);
    }
}
