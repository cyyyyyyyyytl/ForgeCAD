// ============================================================
// MainWindow：主窗口类（多特征版）
// ------------------------------------------------------------
// 职责：
//   ① 加载 .ui（菜单"新建" + 左侧模型树/属性舞台 + 右侧 3D 舞台）
//   ② 持有"特征集合"（std::vector<unique_ptr<Feature>>，多态：Box/Cylinder/Sphere…）
//   ③ 模型树展示所有特征；点树哪项 → 谁就是"当前选中" → 面板和 3D 跟谁
//   ④ 新建 = 追加进集合 + 树加一项 + 自动选中
// 设计思想：
//   · features_ 是唯一真相源，模型树只是它的"展示"（变了就整树重建）
//   · .ui 只摆空舞台，参数界面由代码按选中特征的 parameters() 动态填充
// ============================================================
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>          // std::unique_ptr（持有特征）
#include <vector>          // std::vector（特征集合）

class QStandardItemModel;  // 前向声明：树的"数据模型"（放类外=全局类；指针够用，完整定义在 .cpp）

namespace Ui {
class MainWindow;
}

// 前向声明（避免头文件互相 include）：指针只需要知道"有这么个类"
namespace forge::ui { class Viewport3D; }
namespace forge::domain { class Feature; }   // 基类——任意特征（Box/Cylinder/Sphere…）

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // 菜单动作 → Qt 按名字约定自动连接（on_<动作名>_triggered）
    void on_actionNewBox_triggered();       // 菜单"新建→长方体"
    void on_actionNewCylinder_triggered();  // 菜单"新建→圆柱体"
    void on_actionNewSphere_triggered();    // 菜单"新建→球体"

private:
    Ui::MainWindow *ui;
    forge::ui::Viewport3D* viewport_ = nullptr;      // 3D 视图控件

    // ---- 核心数据：特征集合 + 当前选中下标 ----
    std::vector<std::unique_ptr<forge::domain::Feature>> features_;  // 零件的"仓库"
    int selectedIndex_ = -1;                                        // 当前选中第几个（-1=没选）
    QStandardItemModel* treeModel_ = nullptr;                       // 喂给 modelTree 的数据模型

    // ---- 私有工具（入口与动作分离：槽只转发，逻辑集中在这里）----
    void createFeatureFromDialog(const QString& type); // 弹对话框 → 造特征 → 追加进集合
    QString nextFeatureId(const QString& type);        // 生成唯一身份证（Box001、Cylinder002…）
    void deleteSelectedFeature();                      // 删除当前选中特征，并安全选择相邻项
    void rebuildFeatureTree();                         // 用 features_ 整树重建（集合变了就调）
    void selectFeature(int index);                     // 切换选中：改下标 → 面板重建 → 视图刷新
    void rebuildParamPanel();                          // 按"选中特征"动态重建属性面板
    void refreshViewport();                            // 选中特征 → 重建形状 → 显示 → 状态栏
};

#endif // MAINWINDOW_H
