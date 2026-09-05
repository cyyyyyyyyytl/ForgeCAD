// ============================================================
// MainWindow：主窗口类（升级版：服务"任意特征"）
// ------------------------------------------------------------
// 职责：
//   ① 加载 .ui（菜单"新建" + 左侧属性舞台 + 右侧 3D 舞台）
//   ② 持有"当前特征"（unique_ptr<Feature>，多态：Box/Cylinder...）
//   ③ 菜单新建 → 弹对话框填参数 → FeatureFactory 造特征 → 接管当前模型
//   ④ 属性面板动态化：按当前特征的 parameters() 现生成输入框
// 设计思想：.ui 只摆"空舞台"，参数界面由代码按模型动态填充——
//   以后加新特征类型，UI 代码零改动（数据驱动界面）。
// ============================================================
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>          // std::unique_ptr（持有当前模型）

namespace Ui {
class MainWindow;
}

// 前向声明（避免头文件互相 include）：指针只需要知道"有这么个类"
namespace forge::ui { class Viewport3D; }
namespace forge::domain { class Feature; }   // 基类——任意特征（Box/Cylinder...）

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 菜单动作 → Qt 按名字约定自动连接（on_<动作名>_triggered）
    void on_actionNewBox_triggered();       // 菜单"新建→长方体"被点击
    void on_actionNewCylinder_triggered();  // 菜单"新建→圆柱体"被点击

private:
    Ui::MainWindow *ui;
    forge::ui::Viewport3D* viewport_ = nullptr;      // 3D 视图控件
    std::unique_ptr<forge::domain::Feature> current_; // 当前模型（可以是任意特征类型）

    // ---- 私有工具（入口与动作分离：槽只负责转发，逻辑集中在这里）----
    void createFeatureFromDialog(const QString& type); // 弹对话框 → 造特征 → 接管当前模型
    QString nextFeatureId(const QString& type);        // 生成唯一身份证（Box001、Cylinder002…）
    void rebuildParamPanel();                          // 按当前特征动态重建属性面板
    void refreshViewport();                            // 当前特征 → 重建形状 → 显示 → 状态栏
};

#endif // MAINWINDOW_H
