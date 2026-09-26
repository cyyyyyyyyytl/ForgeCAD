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
// 传统头文件保护宏：防止 MainWindow 类在同一翻译单元中重复定义。
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow> // MainWindow 继承的 Qt 顶层窗口基类。
#include <string>      // 保存当前选中和视口映射使用的稳定 Feature ID。
#include <vector>      // 保存“视口显示下标 -> Feature ID”的顺序映射。

#include "application/ModelDocument.h" // MainWindow 以值成员拥有当前 CAD 文档。

class QStandardItemModel;  // 前向声明：树的"数据模型"（放类外=全局类；指针够用，完整定义在 .cpp）
class QLabel;
class AssistantDialog; // 这里只保存指针，完整对话框定义留给 .cpp。

namespace Ui {
// uic 根据 mainwindow.ui 自动生成这个类；手写头文件只需要知道它的名字。
class MainWindow;
}

// 前向声明（避免头文件互相 include）：指针只需要知道"有这么个类"
namespace forge::ui { class Viewport3D; }
namespace forge::assistant { class AgentController; }

class MainWindow : public QMainWindow
{
    // Q_OBJECT 启用 Qt 元对象系统，使 on_action... 槽可以由名字自动连接。
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr); // 创建界面、视口、模型树和 AI 连接。
    ~MainWindow();                                  // 释放手写 new 的 Ui 包装对象。

private slots:
    void on_actionImportStep_triggered(); // 用户在 Designer 添加同名 QAction 后自动连接。
    void on_actionExportStep_triggered(); // 文件菜单导出当前最终几何。
    // 菜单动作 → Qt 按名字约定自动连接（on_<动作名>_triggered）
    void on_actionNewBox_triggered();       // 菜单"新建→长方体"
    void on_actionNewCylinder_triggered();  // 菜单"新建→圆柱体"
    void on_actionNewSphere_triggered();    // 菜单"新建→球体"
    void on_actionBooleanDifference_triggered();
    void on_actionBooleanUnion_triggered();
    void on_actionBooleanIntersection_triggered();
    void on_actionUndo_triggered();         // 编辑→撤销（Ctrl+Z）
    void on_actionRedo_triggered();         // 编辑→重做（Ctrl+Y）
    void on_actionDeleteFeature_triggered();// 编辑→删除选中特征（Del）

private:
    Ui::MainWindow *ui; // uic 生成界面的访问入口，例如 ui->modelTree。
    forge::ui::Viewport3D* viewport_ = nullptr; // 动态嵌入 viewportContainer 的 3D 控件。

    // ---- 核心数据：文档是模型的唯一所有者和操作入口 ----
    forge::application::ModelDocument document_; // 唯一拥有 Feature 和 Undo/Redo 历史。
    forge::assistant::AgentController* agentController_ = nullptr; // AI 工具循环协调器。
    AssistantDialog* assistantDialog_ = nullptr; // 非模态 AI 对话窗口，重复打开时复用。
    std::string selectedFeatureId_; // 当前树/属性/视口共同选中的稳定对象 ID。
    std::vector<std::string> viewportFeatureIds_; // 对应 Viewport3D 中有效 Shape 的顺序。
    forge::application::ModelDocument::RebuildReport rebuildReport_;
    QLabel* rebuildStatusLabel_ = nullptr; // 当前属性面板中的状态说明，由 Qt 管理。
    QStandardItemModel* treeModel_ = nullptr; // QTreeView 展示所需的数据模型。

    // ---- 私有工具（入口与动作分离：槽只转发，逻辑集中在这里）----
    void createFeatureFromDialog(const QString& type); // 弹对话框 → 造特征 → 追加进集合
    void createBooleanFeatureFromDialog(forge::domain::BooleanOperation operation);
    void rebuildFeatureTree();                         // 用 document_ 整树重建（集合变了就调）
    void selectFeature(const std::string& id);         // 按稳定 ID 切换选中项
    void rebuildParamPanel();                          // 按"选中特征"动态重建属性面板
    void refreshViewport(bool fitAll = true);                            // 选中特征 → 重建形状 → 显示 → 状态栏
    void refreshAfterHistoryChange();                  // Undo/Redo 后统一修正选中项并刷新 UI
    void updateRebuildFeedback();
    void setupAssistantDialog();                       // 连接 Designer AI 窗口与 Agent
};

#endif // MAINWINDOW_H：头文件保护结束。
