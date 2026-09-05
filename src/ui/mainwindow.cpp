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
#include <QTimer>                // 延迟到窗口显示后再执行首次刷新
#include "domain/BoxFeature.h"   // 领域层模型：我们要创建和操作它

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

    // ① 创建"参数化盒子"：id 固定，尺寸 100×50×30（和下方输入框初值一致）
    box_ = std::make_unique<forge::domain::BoxFeature>("Box001", 100.0, 50.0, 30.0);

    // ② 设输入框初值。⚠️必须在创建 box_ 之后：
    //    setValue 会立刻触发 valueChanged → 自动槽会被调用 → 那时 box_ 必须已存在
    ui->spinLength->setValue(100.0);
    ui->spinWidth->setValue(50.0);
    ui->spinHeight->setValue(30.0);

    // ③ 首次显示盒子：必须等事件循环启动、3D 视图就绪（showEvent 已跑过）
    //    才能真的画出来，所以延迟到 0 毫秒后执行
    QTimer::singleShot(0, this, &MainWindow::refreshModel);
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

void MainWindow::on_spinLength_valueChanged(double arg1)
{
    refreshModel();   // 哪个框变了不重要，统一走同一个动作
}


void MainWindow::on_spinWidth_valueChanged(double arg1)
{
    refreshModel();   // 哪个框变了不重要，统一走同一个动作
}


void MainWindow::on_spinHeight_valueChanged(double arg1)
{
    refreshModel();   // 哪个框变了不重要，统一走同一个动作
}

// 把三个输入框的值写进模型 → 重建 → 换图（三个槽和首次显示的公共动作）
void MainWindow::refreshModel()
{
    // ① 面板 → 模型：参数名必须和 BoxFeature 构造时一致（length/width/height）
    box_->setParameter("length", ui->spinLength->value());
    box_->setParameter("width",  ui->spinWidth->value());
    box_->setParameter("height", ui->spinHeight->value());

    // ② 模型 → 形状：重建 = 用新参数重算
    TopoDS_Shape shape = box_->rebuild();

    // ③ 形状 → 屏幕：换图（A 步的"先撤旧再上新"在这里生效）
    if (!shape.IsNull()) {
        viewport_->showShape(shape);
        statusBar()->showMessage(QString("参数化 Box：%1 × %2 × %3")
            .arg(ui->spinLength->value())
            .arg(ui->spinWidth->value())
            .arg(ui->spinHeight->value()));
    }
}
