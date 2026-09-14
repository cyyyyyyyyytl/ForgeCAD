// ============================================================
// MainWindow：主窗口类
// ------------------------------------------------------------
// 职责：
//   ① 加载 .ui（菜单"新建" + 左侧模型树/属性舞台 + 右侧 3D 舞台）
//   ② 持有 ModelDocument；所有建模操作都调用它的公开接口
//   ③ 模型树展示所有特征；点树哪项 → 谁就是"当前选中" → 面板和 3D 跟谁
//   ④ 新建 = 追加进集合 + 树加一项 + 自动选中
// 设计思想：
//   · document_ 是唯一真相源，模型树只是它的"展示"（变了就整树重建）
//   · .ui 只摆空舞台，参数界面由代码按选中特征的 parameters() 动态填充
// ============================================================
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <string>
#include <vector>

#include "application/ModelDocument.h"

class QStandardItemModel;  // 前向声明：树的"数据模型"（放类外=全局类；指针够用，完整定义在 .cpp）
class AssistantDialog;

namespace Ui {
class MainWindow;
}

// 前向声明（避免头文件互相 include）：指针只需要知道"有这么个类"
namespace forge::ui { class Viewport3D; }
namespace forge::assistant { class AgentController; }

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
    void on_actionUndo_triggered();         // 编辑→撤销（Ctrl+Z）
    void on_actionRedo_triggered();         // 编辑→重做（Ctrl+Y）
    void on_actionDeleteFeature_triggered();// 编辑→删除选中特征（Del）

private:
    Ui::MainWindow *ui;
    forge::ui::Viewport3D* viewport_ = nullptr;      // 3D 视图控件

    // ---- 核心数据：文档是模型的唯一所有者和操作入口 ----
    forge::application::ModelDocument document_;
    forge::assistant::AgentController* agentController_ = nullptr;
    AssistantDialog* assistantDialog_ = nullptr;
    std::string selectedFeatureId_;
    std::vector<std::string> viewportFeatureIds_;
    QStandardItemModel* treeModel_ = nullptr;

    // ---- 私有工具（入口与动作分离：槽只转发，逻辑集中在这里）----
    void createFeatureFromDialog(const QString& type); // 弹对话框 → 造特征 → 追加进集合
    void rebuildFeatureTree();                         // 用 document_ 整树重建（集合变了就调）
    void selectFeature(const std::string& id);         // 按稳定 ID 切换选中项
    void rebuildParamPanel();                          // 按"选中特征"动态重建属性面板
    void refreshViewport();                            // 选中特征 → 重建形状 → 显示 → 状态栏
    void refreshAfterHistoryChange();                  // Undo/Redo 后统一修正选中项并刷新 UI
    void setupAssistantDialog();                       // 连接 Designer AI 窗口与 Agent
};

#endif // MAINWINDOW_H
