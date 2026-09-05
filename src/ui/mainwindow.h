// ============================================================
// MainWindow：主窗口类
// ------------------------------------------------------------
// 职责：
//   ① 加载可视化界面(mainwindow.ui：左侧模型树 + 右侧3D容器)
//   ② 内部创建 Viewport3D 并放进 viewportContainer(正规写法,用 ui->)
//   ③ 对外只暴露"能干什么"的接口(showBox),不暴露内部控件
// 设计：封装——外部(main.cpp)通过接口操作,不翻内部控件
// ============================================================
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <TopoDS_Shape.hxx>      // OCCT 形状类型(showBox 的参数)
#include <memory>        // std::unique_ptr：BoxFeature 成员用智能指针持有
namespace Ui {
class MainWindow;
}

// 前向声明：3D 视图控件(避免头文件互相 include)
namespace forge::ui { class Viewport3D; }
namespace forge::domain { class BoxFeature; }   // 先只声明存在，完整定义在 .cpp 里才需要

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 对外接口：显示一个 OCCT 形状到 3D 视图
    // 外部(main.cpp)只管"给一个形状",不管内部怎么显示
    void showBox(const TopoDS_Shape& shape);

private slots:
    // Qt Creator "转到槽" 生成的槽：控件值一变 → Qt 按名字约定自动调用
    // （on_<控件名>_<信号名>；必须声明在 slots 区，moc 才认识它们）
    void on_spinLength_valueChanged(double arg1);
    void on_spinWidth_valueChanged(double arg1);
    void on_spinHeight_valueChanged(double arg1);

private:
    Ui::MainWindow *ui;
    forge::ui::Viewport3D* viewport_ = nullptr;   // 3D 视图控件(类内部持有)
    std::unique_ptr<forge::domain::BoxFeature> box_;   // 程序当前这个"参数化盒子"，面板改的就是它
    void refreshModel();   // 公共动作：把面板值写进模型 → 重建 → 换图（三个槽都调它，避免重复代码）
};

#endif // MAINWINDOW_H
