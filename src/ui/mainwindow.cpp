// ============================================================
// MainWindow 实现
// ------------------------------------------------------------
// 关键：Viewport3D 在【构造函数内部】创建，直接放进
//       ui->viewportContainer(界面里的容器)。
//       —— 这是"类内部用 ui-> 访问控件"的正规写法。
// ============================================================
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QVBoxLayout>          // 垂直布局(给容器铺)
#include "ui/Viewport3D.h"      // 3D 视图控件(自己写的)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);          // 从 mainwindow.ui 加载界面(建好所有控件)

    // ---- 在界面右侧容器里创建 3D 视图(正规写法: ui->直接访问) ----
    // viewportContainer 是你拖在界面右侧的空容器(QWidget)
    auto* vlayout = new QVBoxLayout(ui->viewportContainer);   // 给容器铺垂直布局
    vlayout->setContentsMargins(0, 0, 0, 0);                  // 不留边距(占满)
    viewport_ = new forge::ui::Viewport3D(ui->viewportContainer);  // 创建3D视图,爸爸是容器
    vlayout->addWidget(viewport_);                            // 放进去,占满
}

MainWindow::~MainWindow()
{
    delete ui;
}

// 对外接口：显示形状到 3D 视图
void MainWindow::showBox(const TopoDS_Shape& shape)
{
    if (viewport_) {
        viewport_->showShape(shape);   // 转发给 3D 视图
    }
}
