// ============================================================
// MainWindow 实现（多特征 + 模型树版）
// ------------------------------------------------------------
// 本文件是"集合驱动界面"的样板：
//   · document_ 是唯一真相源；模型树只是它的展示
//   · UI 和 AI 都通过 ModelDocument 修改模型
//   · 点树 → selectFeature() 换选中 → 属性面板和 3D 视图跟着切
//   · 属性面板每行输入框变化 → setParameter → rebuild → showShapes（实时联动）
// ============================================================
#include "mainwindow.h"    // MainWindow 手写类声明和持有的 ModelDocument。
#include "ui_mainwindow.h" // uic 根据 mainwindow.ui 自动生成，只使用、不手改。

#include <QVBoxLayout>          // 垂直布局（3D 视图/分组框占满用）
#include <QFormLayout>          // 表单布局（"标签 + 输入框"一行行排）
#include <Standard_Failure.hxx>
#include "infrastructure/StepIO.h"
#include <QFileInfo>
#include "domain/ImportedFeature.h"
#include "domain/PositionParameters.h"
#include <QFileDialog>
#include <QDialog>              // 新建对话框
#include <QDialogButtonBox>     // 对话框的 确定/取消 按钮组
#include <QPushButton>          // 确定/取消 按钮（改中文文字用）
#include <QDoubleSpinBox>       // 数字输入框
#include <QComboBox>            // 按稳定 Feature ID 选择布尔运算的两个输入。
#include "ui/RebuildFeedback.h"
#include <QLabel>               // 解释差集的输入顺序。
#include <QMessageBox>          // 级联删除时向用户列出受影响的特征。
#include <QString>              // 字符串（中文标签/提示用）
#include <QStringList>          // 将受影响 ID 排版为确认框中的逐行清单。
#include <QStatusBar>           // 状态栏
#include <QStandardItemModel>   // 模型树的数据模型（含 QStandardItem）
#include <QTreeView>            // 模型树控件（clicked 信号）

#include "ui/Viewport3D.h"      // 3D 视图控件
#include "assistantdialog.h"              // AI 非模态对话框的手写控制类。
#include "assistant/AgentController.h"    // 对话历史和模型-工具循环协调器。
#include "domain/Feature.h"               // 读取多态 Feature 参数并调用 rebuild。
#include "domain/BooleanFeature.h"        // 三个菜单共用同一套布尔特征创建流程。
#include "domain/FeatureRegistry.h"       // 动态生成新建/属性输入框的参数规则。

#include <vector>               // 保存控件、分类顺序和有效 Shape 列表。
#include <map>                  // rebuildFeatureTree 按类型临时分组 Feature。
#include <string>               // 稳定 ID、类型名和参数名使用标准字符串。
#include <stdexcept>            // std::invalid_argument（捕获工厂的抛错）

namespace {

// ------------------------------------------------------------
// 参数英文名 → 中文标签（翻译是 UI 层的职责，domain 保持语言无关）
// ------------------------------------------------------------
QString paramLabel(const std::string& en)
{
    if (en == "x") return QStringLiteral("位置 X");
    if (en == "y") return QStringLiteral("位置 Y");
    if (en == "z") return QStringLiteral("位置 Z");
    if (en == "length") return QStringLiteral("长度");
    if (en == "width")  return QStringLiteral("宽度");
    if (en == "height") return QStringLiteral("高度");
    if (en == "radius") return QStringLiteral("半径");
    return QString::fromStdString(en);   // 还没翻译的新参数先显示原名
}

// ------------------------------------------------------------
// 类型名(domain 契约) → 中文名（菜单/对话框标题共用）
// ------------------------------------------------------------
QString kindLabel(const QString& kind)
{
    if (kind == QStringLiteral("Imported")) return QStringLiteral("STEP 导入");
    if (kind == QStringLiteral("Box"))      return QStringLiteral("长方体");
    if (kind == QStringLiteral("Cylinder")) return QStringLiteral("圆柱体");
    if (kind == QStringLiteral("Sphere"))   return QStringLiteral("球体");
    if (kind == QStringLiteral("Cut"))      return QStringLiteral("差集");
    if (kind == QStringLiteral("Union"))    return QStringLiteral("并集");
    if (kind == QStringLiteral("Intersection")) return QStringLiteral("交集");
    return kind;   // 还没加中文名的类型先用原名
}

} // namespace

// ============================================================
// 构造函数
// ============================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)       // 建立 Qt 父子关系并初始化顶层窗口基类。
    , ui(new Ui::MainWindow)    // 创建 uic 生成的界面包装对象。
{
    ui->setupUi(this);          // 从 .ui 加载界面（含菜单、模型树、空属性舞台、3D 容器）

    // ---- 属性分组框内部整理：容器填满分组框、四边留 8px ----
    auto* groupLayout = new QVBoxLayout(ui->groupBox); // 以 groupBox 为父对象自动管理布局。
    groupLayout->setContentsMargins(8, 8, 8, 8);       // 标题框内部保留均匀留白。
    groupLayout->addWidget(ui->paramPanelContainer);    // 空容器占满可用属性区域。

    // ---- 3D 视图嵌入右侧容器 ----
    auto* vlayout = new QVBoxLayout(ui->viewportContainer); // 容器负责管理视口尺寸。
    vlayout->setContentsMargins(0, 0, 0, 0);                // 3D 画面贴合整个容器。
    viewport_ = new forge::ui::Viewport3D(ui->viewportContainer); // 父对象自动释放视口。
    vlayout->addWidget(viewport_); // 布局让视口随主窗口缩放。

    // 3D 拾取反向驱动模型树和属性面板，形成双向选择联动。
    connect(viewport_, &forge::ui::Viewport3D::shapeSelected, this,
            [this](int index) {
                if (index < 0
                    || index >= static_cast<int>(viewportFeatureIds_.size())) {
                    selectedFeatureId_.clear();               // 清除业务层选中 ID。
                    ui->modelTree->clearSelection();           // 清除树的选择高亮。
                    ui->modelTree->setCurrentIndex(QModelIndex()); // 清除当前索引。
                    rebuildParamPanel();                       // 无选择时清空属性区。
                    ui->actionDeleteFeature->setEnabled(false); // 禁止执行无目标删除。
                    statusBar()->showMessage(QStringLiteral("未选择特征"));
                    return;
                }

                selectFeature(viewportFeatureIds_[index]);
                rebuildFeatureTree();
            });

    // ---- 模型树接线：QTreeView 需要"数据模型"才有内容 ----
    treeModel_ = new QStandardItemModel(this);   // 父对象 = this，Qt 自动释放
    ui->modelTree->setModel(treeModel_); // QTreeView 只显示，不自行保存条目数据。
    // 用户点树的某一行 → 切到那一行对应的特征
    // 树现在是"分类文件夹 + 特征子项"结构，节点用 UserRole 保存稳定 Feature ID。
    connect(ui->modelTree, &QTreeView::clicked, this,
            [this](const QModelIndex& idx) {
                if (!idx.isValid()) return; // 无效点击位置不对应任何树节点。
                const QVariant data = treeModel_->data(idx, Qt::UserRole);
                if (!data.isValid()) return;      // 点的是分类文件夹（没存下标）→ 忽略
                selectFeature(data.toString().toStdString());
            });

    // ---- 空文档启动：不预置任何特征 ----
    // 文档为空、selectedFeatureId_ 为空（没选中任何东西）。
    // 后面所有函数都会先按 ID 查找，空文档时安全跳过。
    rebuildFeatureTree();    // 空树（啥也没有，正常）
    rebuildParamPanel();     // 空面板（容器刚被清空）
    setupAssistantDialog(); // 创建一次 AI 窗口和 Controller，并完成全部信号连接。
    statusBar()->showMessage(QStringLiteral("用菜单\"新建\"添加你的第一个特征"));
    // 注意：这里不再 QTimer 首图——没有特征可显示；等用户新建第一个时
    // createFeatureFromDialog 会自己刷新视图（那时窗口早已显示、视图已就绪）。
}

MainWindow::~MainWindow()
{
    delete ui;   // Qt 子控件由父对象释放；document_ 用 unique_ptr 自动释放 Feature
}

// ============================================================
// 槽：菜单"新建→长方体 / 圆柱体 / 球体"
// ============================================================
void MainWindow::on_actionNewBox_triggered()
{
    createFeatureFromDialog(QStringLiteral("Box"));
}

void MainWindow::on_actionNewCylinder_triggered()
{
    createFeatureFromDialog(QStringLiteral("Cylinder"));
}

void MainWindow::on_actionNewSphere_triggered()
{
    createFeatureFromDialog(QStringLiteral("Sphere"));
}

void MainWindow::on_actionBooleanDifference_triggered()
{
    createBooleanFeatureFromDialog(forge::domain::BooleanOperation::Difference);
}

void MainWindow::on_actionBooleanUnion_triggered()
{
    createBooleanFeatureFromDialog(forge::domain::BooleanOperation::Union);
}

void MainWindow::on_actionBooleanIntersection_triggered()
{
    createBooleanFeatureFromDialog(forge::domain::BooleanOperation::Intersection);
}

void MainWindow::on_actionUndo_triggered()
{
    if (!document_.canUndo()) {
        statusBar()->showMessage(QStringLiteral("没有可以撤销的操作"));
        return;
    }

    document_.undo();            // 领域状态先恢复到上一张快照。
    refreshAfterHistoryChange(); // 再让树、属性面板和视口同步新状态。
}

void MainWindow::on_actionRedo_triggered()
{
    if (!document_.canRedo()) {
        statusBar()->showMessage(QStringLiteral("没有可以重做的操作"));
        return;
    }

    document_.redo();            // 领域状态前进到 Redo 栈顶快照。
    refreshAfterHistoryChange(); // 所有展示层从 Document 重新读取。
}

void MainWindow::on_actionDeleteFeature_triggered()
{
    if (!document_.findFeature(selectedFeatureId_)) {
        statusBar()->showMessage(QStringLiteral("请先在模型树中选择要删除的特征"));
        return;
    }

    try {
        const auto removal = document_.deletionOrder(selectedFeatureId_);
        if (removal.size() > 1) {
            QStringList dependentIds;
            for (const std::string& id : removal) {
                if (id != selectedFeatureId_) {
                    dependentIds.append(QString::fromStdString(id));
                }
            }
            const QString prompt = QStringLiteral("删除 %1 会同时删除以下依赖特征：\n%2\n\n确定继续吗？")
                                       .arg(QString::fromStdString(selectedFeatureId_),
                                            dependentIds.join("\n"));
            if (QMessageBox::question(this, QStringLiteral("确认删除"), prompt,
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No) != QMessageBox::Yes) {
                return;
            }
        }
        document_.deleteFeature(selectedFeatureId_);
        // 删除后原选中 ID 已失效；沿用历史刷新策略选择剩余文档的末项，
        // 若删除的是最后一个特征，则同时清空属性面板和三维场景。
        refreshAfterHistoryChange();
    } catch (const std::invalid_argument& e) {
        statusBar()->showMessage(
            QStringLiteral("删除失败: %1").arg(e.what()));
    }
}

// ============================================================
// createFeatureFromDialog：弹对话框 → 造特征 → 追加进集合并选中
// ============================================================
void MainWindow::createBooleanFeatureFromDialog(forge::domain::BooleanOperation operation)
{
    const auto& features = document_.features();
    if (features.size() < 2) {
        QMessageBox::information(this, QStringLiteral("布尔运算"),
                                 QStringLiteral("请先创建至少两个特征。"));
        return;
    }

    QString title;
    switch (operation) {
    case forge::domain::BooleanOperation::Difference: title = QStringLiteral("差集"); break;
    case forge::domain::BooleanOperation::Union: title = QStringLiteral("并集"); break;
    case forge::domain::BooleanOperation::Intersection: title = QStringLiteral("交集"); break;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("booleanFeatureDialog"));
    dialog.setWindowTitle(title);
    auto* form = new QFormLayout(&dialog);
    auto* base = new QComboBox(&dialog);
    auto* tool = new QComboBox(&dialog);
    base->setObjectName(QStringLiteral("booleanBaseCombo"));
    tool->setObjectName(QStringLiteral("booleanToolCombo"));

    // 显示中文类型，itemData 保存稳定 ID；创建时不依赖文本或容器下标。
    for (const auto& feature : features) {
        const QString id = QString::fromStdString(feature->id());
        const QString label = id + QStringLiteral(" · ")
            + kindLabel(QString::fromStdString(feature->type()));
        base->addItem(label, id);
        tool->addItem(label, id);
    }
    const int selected = base->findData(QString::fromStdString(selectedFeatureId_));
    base->setCurrentIndex(selected >= 0 ? selected : 0);
    tool->setCurrentIndex(base->currentIndex() == 0 ? 1 : 0);
    form->addRow(QStringLiteral("主体"), base);
    form->addRow(QStringLiteral("工具形状"), tool);
    if (operation == forge::domain::BooleanOperation::Difference) {
        form->addRow(new QLabel(QStringLiteral("结果 = 主体 − 工具形状"), &dialog));
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto updateConfirmation = [base, tool, buttons]() {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(base->currentData() != tool->currentData());
    };
    connect(base, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, updateConfirmation);
    connect(tool, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, updateConfirmation);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    updateConfirmation();

    if (dialog.exec() != QDialog::Accepted) return;
    try {
        auto& feature = document_.createBooleanFeature(
            operation, base->currentData().toString().toStdString(),
            tool->currentData().toString().toStdString());
        selectedFeatureId_ = feature.id();
        refreshAfterHistoryChange(); // 同步树、属性面板和只显示最终结果的视图。
    } catch (const std::exception& e) {
        QMessageBox::warning(this, QStringLiteral("创建失败"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::createFeatureFromDialog(const QString& type)
{
    // ① 取出该类型要哪些参数（没有 → 类型不受支持，直接返回）
    const auto* descriptor = forge::domain::FeatureRegistry::find(type.toStdString());
    if (!descriptor) return; // 未登记类型没有参数规则，不能构造输入界面。

    // ② 用代码现造对话框：标题 + 每个参数一行（标签+数字框）
    QDialog dlg(this); // 栈上模态对话框在函数结束时自动销毁。
    dlg.setWindowTitle(QStringLiteral("新建") + kindLabel(type));   // "新建球体"…
    auto* form = new QFormLayout(&dlg);       // 表单布局：一行 = 标签 + 输入框
    std::vector<QDoubleSpinBox*> spins;       // 记下所有输入框，确定后好取值

    for (const auto& parameter : descriptor->parameters) {
        auto* spin = new QDoubleSpinBox(&dlg); // dlg 作为父对象自动管理输入框。
        spin->setObjectName(QString::fromStdString(parameter.name));
        spin->setRange(parameter.minimum, parameter.maximum); // 套用 Registry 统一范围。
        spin->setDecimals(3);
        spin->setSuffix(QStringLiteral(" mm")); // 尺寸与位置统一显示毫米和三位小数。
        spin->setValue(parameter.defaultValue); // 使用 Descriptor 的默认创建值。
        form->addRow(paramLabel(parameter.name), spin); // 一行加入中文标签和输入框。
        spins.push_back(spin); // 保留与 Descriptor 相同的顺序，确定后读取数值。
    }

    const QString reference = type == "Box" ? QStringLiteral("基准角点")
        : type == "Cylinder" ? QStringLiteral("底面圆心") : QStringLiteral("球心");
    form->addRow(new QLabel(QStringLiteral("位置为世界坐标系中的%1，单位毫米。坐标轴方向保持不变。")
                           .arg(reference), &dlg));

    // ③ 底部放 确定/取消 按钮组，并把按钮文字改成中文
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                       | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定")); // 本地化确认按钮。
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); // 本地化取消按钮。
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    // ④ exec() 阻塞等待用户点按钮：确定=Accepted，取消=Rejected
    if (dlg.exec() != QDialog::Accepted) {
        return;                       // 用户取消 → 什么都不做
    }

    // ⑤ 收集数值 → 工厂造特征（个数不对/未知类型会抛，捕获并提示）
    forge::domain::NumericParameters parameters;
    for (std::size_t i = 0; i < descriptor->parameters.size(); ++i) {
        // 描述和输入框按同一顺序保存，因此可安全组合为“参数名 -> 用户值”。
        parameters.emplace(descriptor->parameters[i].name, spins[i]->value());
    }

    try {
        auto& feature = document_.createFeature(type.toStdString(), parameters);
        selectedFeatureId_ = feature.id(); // 新建成功后自动选中新对象。
    } catch (const std::invalid_argument& e) {
        statusBar()->showMessage(QStringLiteral("创建失败: %1").arg(e.what()));
        return;
    }

    // ⑥ 集合变了：树加一项 + 面板跟新特征 + 出图
    rebuildFeatureTree();
    rebuildParamPanel();
    refreshViewport();
}

// ============================================================
// rebuildFeatureTree：用 document_ 整树重建（按类型分组）
// ------------------------------------------------------------
// 结构：
//   长方体                 ← 分类"文件夹"（文字 = 中文类型名）
//    ├─ Box001             ← 特征子项（文字 = id；节点里存稳定 ID 到 UserRole）
//    └─ Box002
//   圆柱体
//    └─ Cylinder001
// document_ 是唯一真相源，树只是展示：每次集合变了就清空重建，并恢复选中高亮。
// 点击子项时从 UserRole 取出 Feature ID → selectFeature。
// ============================================================
void MainWindow::rebuildFeatureTree()
{
    treeModel_->clear();                      // 清空旧树

    // ① 按"首次出现顺序"把特征归类：kind -> 属于它的集合下标们
    std::vector<QString> kindOrder;                          // 分类顺序（如 长方体、圆柱体…）
    std::map<QString, std::vector<int>> featuresByKind;      // 类型名 -> 下标列表
    const auto& features = document_.features(); // 借用文档容器，只读且不复制 unique_ptr。
    for (int i = 0; i < static_cast<int>(features.size()); ++i) {
        const QString kind = QString::fromStdString(features[i]->type());  // "Box"/"Cylinder"…
        if (featuresByKind.find(kind) == featuresByKind.end()) {
            kindOrder.push_back(kind);       // 第一次见到这个类型 → 记下它的位置
        }
        featuresByKind[kind].push_back(i); // 分类保存原文档下标，仍能取回真实对象。
    }

    // ② 一个类型一个"文件夹"，特征作为它的子项
    QStandardItem* selectedTreeItem = nullptr;    // 记住选中项对应的树节点（用于恢复高亮）
    for (const QString& kind : kindOrder) {
        auto* cat = new QStandardItem(kindLabel(kind));   // 文件夹文字：中文类型名
        cat->setEditable(false); // UI 树名称不允许直接编辑，避免与 Document 失同步。

        for (int i : featuresByKind[kind]) {
            auto* child = new QStandardItem(
                QString::fromStdString(features[i]->id()));   // 子项文字：id（如 Box002）
            child->setEditable(false); // Feature ID 由 Document 管理，界面不能重命名。
            // UserRole 保存稳定 ID；显示文字变化时也不依赖行号定位对象。
            child->setData(QString::fromStdString(features[i]->id()), Qt::UserRole);
            if (features[i]->id() == selectedFeatureId_) selectedTreeItem = child;
            cat->appendRow(child); // 把具体 Feature 放入对应类型分类节点。
        }
        treeModel_->appendRow(cat);            // 文件夹进树
    }

    // ③ 分类默认全展开 + 恢复选中高亮
    ui->modelTree->expandAll();
    if (selectedTreeItem) {
        ui->modelTree->setCurrentIndex(treeModel_->indexFromItem(selectedTreeItem));
    }

    // 空文档或没有有效选择时，禁用 Designer 中的删除 Action。
    ui->actionDeleteFeature->setEnabled(
        document_.findFeature(selectedFeatureId_) != nullptr);
    updateRebuildFeedback(); // 选择联动重建树时，也保留已计算的状态和颜色。
}

// ============================================================
// selectFeature：切换当前选中的特征
// ------------------------------------------------------------
// 树点击、新建后自动选中，都走这里：换稳定 ID → 面板重建 → 视图刷新
// ============================================================
void MainWindow::selectFeature(const std::string& id)
{
    const auto* feature = document_.findFeature(id);
    if (!feature) return;      // 失效 ID 不改变当前界面状态。
    selectedFeatureId_ = id;   // 所有 UI 联动都以这一稳定 ID 为真值。
    rebuildParamPanel();    // 面板换成这个特征的参数

    int selectedShapeIndex = -1;
    for (int i = 0; i < static_cast<int>(viewportFeatureIds_.size()); ++i) {
        if (viewportFeatureIds_[i] == id) {
            selectedShapeIndex = i; // 找到视口中对应 AIS_Shape 的显示下标。
            break;                  // ID 唯一，命中后停止扫描。
        }
    }
    viewport_->setSelectedIndex(selectedShapeIndex);
    ui->actionDeleteFeature->setEnabled(true);
    statusBar()->showMessage(
        QStringLiteral("共 %1 个特征 · 已显示 %2 个 · 当前: %3 (%4)")
            .arg(document_.features().size())
            .arg(viewportFeatureIds_.size())
            .arg(QString::fromStdString(feature->type()),
                 QString::fromStdString(feature->id())));
    updateRebuildFeedback();
}

// ============================================================
// rebuildParamPanel：按"选中特征"的参数重造属性面板
// ============================================================
void MainWindow::rebuildParamPanel()
{
    // ① 先清掉容器里上一次的输入框（布局和控件都要删）
    rebuildStatusLabel_ = nullptr; // 旧控件稍后释放，不继续保留借用指针。
    QLayout* oldLayout = ui->paramPanelContainer->layout(); // 可能为空或属于上次选择。
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget()) w->deleteLater(); // 安排旧输入框安全延迟销毁。
            delete item; // QLayoutItem 包装本身不归控件父子树管理，要立即释放。
        }
        delete oldLayout; // 所有子项取出后释放旧布局对象。
    }

    // ② 没有选中任何特征 → 面板留空
    auto* feature = document_.findFeature(selectedFeatureId_);
    if (!feature) return; // 没有有效选择时保持空面板。
    const std::string featureId = feature->id(); // lambda 按值保存 ID，不长期保存裸指针。

    // ③ 按选中特征的真实参数生成输入框——parameters()（规矩①）驱动 UI！
    auto* form = new QFormLayout(ui->paramPanelContainer);
    form->setContentsMargins(12, 16, 12, 12);   // 上边距加大：别贴着"属性"标题
    form->setVerticalSpacing(10);               // 行距
    form->setHorizontalSpacing(12);             // 标签和输入框的间距

    const auto& params = feature->parameters();

    for (const auto& p : params) {
        auto* spin = new QDoubleSpinBox(ui->paramPanelContainer); // 父容器管理生命周期。
        spin->setDecimals(3);
        spin->setSuffix(QStringLiteral(" mm")); // 与新建对话框保持一致的尺寸精度。
        // 键盘输入“50”时，默认会先为字符“5”发出一次 valueChanged，
        // 再为最终值“50”发出第二次，导致 Undo 历史记录两个中间状态。
        // 关闭 keyboardTracking 后，键盘编辑只在回车或失去焦点时提交最终值；
        // 点击上下箭头仍会正常发出 valueChanged，并保持即时建模反馈。
        spin->setKeyboardTracking(false);
        if (const auto* descriptor = forge::domain::FeatureRegistry::findParameter(
                feature->type(), p.name())) {
            spin->setRange(descriptor->minimum, descriptor->maximum);
        }
        spin->setObjectName(QString::fromStdString(p.name()));
        spin->setValue(p.asDouble());         // 初值 = 模型当前值
        const bool imported = dynamic_cast<const forge::domain::ImportedFeature*>(feature) != nullptr;
        const QString label = imported && forge::domain::isPositionParameter(p.name())
            ? QStringLiteral("平移 %1").arg(QString::fromStdString(p.name()).toUpper()) : paramLabel(p.name());
        form->addRow(label, spin); // 导入位置是额外平移，避免被误认为原始几何基准点。

        // ★ 动态控件没有固定的 on_ 名字 → 手动 connect（lambda 捕获参数名）。
        //   箭头调整会立即提交；键盘输入则在编辑完成后只提交最终数值。
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, featureId, paramName = p.name()](double v) {
                    try {
                        document_.setParameter(featureId, paramName, v);
                        refreshViewport(false); // 保持观察视角，便于判断位置和尺寸的变化。
                    } catch (const std::invalid_argument& e) {
                        statusBar()->showMessage(
                            QStringLiteral("修改失败: %1").arg(e.what()));
                    }
                });
    }
    rebuildStatusLabel_ = new QLabel(ui->paramPanelContainer);
    rebuildStatusLabel_->setObjectName(QStringLiteral("rebuildStatusLabel"));
    rebuildStatusLabel_->setWordWrap(true);
    rebuildStatusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (const auto* imported = dynamic_cast<const forge::domain::ImportedFeature*>(feature)) {
        auto* info = new QLabel(QStringLiteral("来源：%1\n位置为相对文件原始几何的平移；原始尺寸由 STEP 几何决定。")
            .arg(QString::fromStdString(imported->sourceName())), ui->paramPanelContainer);
        info->setTextFormat(Qt::PlainText);
        info->setWordWrap(true);
        form->addRow(info);
    }
    form->addRow(QStringLiteral("重建状态"), rebuildStatusLabel_);
    updateRebuildFeedback();

}

// ============================================================
// refreshViewport：重建全部形状 → 同时显示 → 高亮选中项 → 状态栏汇报
// ============================================================
void MainWindow::refreshViewport(bool fitAll)
{
    if (!viewport_) return;                                      // 3D 视图还没就绪（启动早期）

    // ① 文档按依赖顺序重建全部几何，视图只绘制未被布尔运算使用的结果。
    // validShapes 只收集成功生成的形状；selectedShapeIndex 记录当前选中项
    // 在“有效形状列表”里的位置，随后交给 Viewport3D 做高亮。
    std::vector<TopoDS_Shape> validShapes;
    viewportFeatureIds_.clear(); // 重新建立与本轮有效 Shape 完全一致的映射。
    const auto& features = document_.features();
    validShapes.reserve(features.size()); // 最多每个 Feature 产生一个 Shape。
    viewportFeatureIds_.reserve(features.size()); // 映射项数上限相同。
    int selectedShapeIndex = -1;
    rebuildReport_ = document_.rebuildReport();
    for (const auto& featureId : document_.visibleFeatureIds(rebuildReport_)) {
        const auto& result = rebuildReport_.at(featureId);
        TopoDS_Shape shape = result.shape;
        if (result.status != forge::core::RebuildStatus::Ready || shape.IsNull()) {
            continue;                                  // 生成失败的空形状不交给显示层
        }

        if (featureId == selectedFeatureId_) {
            selectedShapeIndex = static_cast<int>(validShapes.size());
        }
        validShapes.push_back(shape); // 仅把非空 OCCT Shape 交给显示层。
        viewportFeatureIds_.push_back(featureId); // 同步记录同位置 Feature ID。
    }

    // ② Viewport3D 只认识 TopoDS_Shape，不认识 Feature：继续保持业务与显示解耦。
    viewport_->showShapes(validShapes, selectedShapeIndex, fitAll);

    updateRebuildFeedback();

}

// ============================================================
// refreshAfterHistoryChange：Undo/Redo 后同步模型树、属性面板和三维视图
// ------------------------------------------------------------
// 撤销“创建”会让当前选中的 Feature 从 Document 消失，因此必须先修正选择：
// 优先选中文档末尾仍存在的 Feature；若文档已空，则清空选择和 3D 场景。
// ============================================================
void MainWindow::refreshAfterHistoryChange()
{
    if (!document_.findFeature(selectedFeatureId_)) {
        const auto& features = document_.features();
        // 原对象若被撤销/删除，优先选择剩余文档末项；空文档则清空选择。
        selectedFeatureId_ = features.empty() ? std::string() : features.back()->id();
    }

    rebuildFeatureTree();
    rebuildParamPanel();

    if (document_.features().empty()) {
        if (viewport_) {
            viewport_->showShapes({}, -1);
        }
        rebuildReport_.clear();
        viewportFeatureIds_.clear(); // 场景为空时映射也必须为空，防止旧下标残留。
        statusBar()->showMessage(QStringLiteral("文档为空"));
        return;
    }

    refreshViewport();
}

void MainWindow::updateRebuildFeedback()
{
    using forge::core::RebuildStatus;
    // 只更新树的数据，不重建属性控件，避免打断正在进行的参数编辑。
    int failures = 0, empty = 0;
    for (int row = 0; row < treeModel_->rowCount(); ++row) {
        auto* category = treeModel_->item(row);
        for (int childRow = 0; childRow < category->rowCount(); ++childRow) {
            auto* item = category->child(childRow);
            const QString id = item->data(Qt::UserRole).toString();
            const auto found = rebuildReport_.find(id.toStdString());
            if (found == rebuildReport_.end()) continue;
            const auto& result = found->second;
            const bool failed = !result.usable();
            failures += failed;
            empty += result.status == RebuildStatus::Empty;
            forge::ui::decorateRebuildItem(*item, id, result);
        }
    }
    const auto selected = rebuildReport_.find(selectedFeatureId_);
    QString selectedMessage;
    if (selected != rebuildReport_.end()) {
        selectedMessage = forge::ui::rebuildStatusLabel(selected->second.status) + "：" + QString::fromStdString(selected->second.message);
        if (rebuildStatusLabel_) {
            rebuildStatusLabel_->setText(selectedMessage);
            rebuildStatusLabel_->setStyleSheet(!selected->second.usable() ? "color: #be2d2d;" : "");
        }
    } else if (rebuildStatusLabel_) {
        rebuildStatusLabel_->setText(QStringLiteral("尚未计算"));
    }
    statusBar()->showMessage(QStringLiteral("共 %1 个特征 · 显示 %2 个 · 失败 %3 个 · 空结果 %4 个%5")
        .arg(document_.features().size()).arg(viewportFeatureIds_.size()).arg(failures).arg(empty)
        .arg(selectedMessage.isEmpty() ? QString() : QStringLiteral(" · %1：%2")
            .arg(QString::fromStdString(selectedFeatureId_), selectedMessage)));
}

void MainWindow::setupAssistantDialog()
{
    // 顶层对话框必须由程序实例化，但其中所有可见控件均来自 assistantdialog.ui。
    assistantDialog_ = new AssistantDialog(this);

    // 工具栏的 Designer Action 打开同一个非模态窗口；重复点击不会创建多份会话。
    connect(ui->actionAI, &QAction::triggered, this, [this]() {
        assistantDialog_->show();           // 首次显示或从隐藏状态恢复窗口。
        assistantDialog_->raise();          // 把已有窗口提升到同应用其他窗口之前。
        assistantDialog_->activateWindow(); // 请求窗口系统把键盘焦点交给它。
    });

    // AgentController 是 QObject 子对象，MainWindow 析构时由 Qt 自动释放。
    // 它引用的 document_ 是 MainWindow 成员，生命周期更长。
    agentController_ = new forge::assistant::AgentController(
        document_, this);

    connect(assistantDialog_, &AssistantDialog::messageSubmitted,
            this, [this](const QString& message) {
                // 对话框已经有一层禁用保护；这里再检查一次避免竞态式重复提交。
                if (!agentController_->isBusy()) agentController_->submit(message);
            });

    // 正常回答和错误使用不同前缀，让用户能快速区分模型回复与请求故障。
    connect(agentController_, &forge::assistant::AgentController::assistantMessage,
            assistantDialog_, &AssistantDialog::appendAssistantMessage);
    connect(agentController_, &forge::assistant::AgentController::errorMessage,
            assistantDialog_, &AssistantDialog::appendErrorMessage);
    // 短暂的执行阶段提示放入状态栏，避免用技术细节污染对话历史。
    connect(agentController_, &forge::assistant::AgentController::statusMessage,
            this, [this](const QString& message) {
                statusBar()->showMessage(message);
            });
    // 网络请求和工具循环执行期间锁住输入，既给出视觉反馈，也形成第二层防重入保护。
    connect(agentController_, &forge::assistant::AgentController::busyChanged,
            assistantDialog_, &AssistantDialog::setBusy);
    // 工具真正修改模型后才刷新 UI；普通问答和查询不会触发不必要的 OCCT 重建。
    connect(agentController_, &forge::assistant::AgentController::modelChanged,
            this, [this](const QString& featureId) {
                // 创建/修改时选中新对象；删除时 ID 已不存在，需走空文档也能处理的刷新路径。
                selectedFeatureId_ = featureId.toStdString();

                if (!document_.findFeature(selectedFeatureId_)) {
                    refreshAfterHistoryChange();
                    return;
                }

                rebuildFeatureTree();
                rebuildParamPanel();
                refreshViewport();
            });
}


void MainWindow::on_actionExportStep_triggered()
{
    try {
        // 收集策略属于文档层；UI 不推断依赖，也不导出失败时展开的诊断形状。
        const auto geometry = document_.shapeForExport();
        if (geometry.status != forge::core::RebuildStatus::Ready) {
            QMessageBox::warning(this, QStringLiteral("无法导出 STEP"),
                QString::fromStdString(geometry.message));
            return;
        }

        QFileDialog dialog(this, QStringLiteral("导出 STEP"));
        dialog.setObjectName(QStringLiteral("stepExportDialog"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setNameFilter(QStringLiteral("STEP 文件 (*.step *.stp)"));
        dialog.setDefaultSuffix(QStringLiteral("step"));
        dialog.selectFile(QStringLiteral("model.step"));
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;

        const QString path = dialog.selectedFiles().first();
        const auto result = forge::infrastructure::StepIO::exportShape(geometry.shape, path);
        if (!result.success) {
            QMessageBox::warning(this, QStringLiteral("STEP 导出失败"), result.error);
            return;
        }
        // 导出不改动文档或 Undo/Redo，也不刷新相机、选择和属性面板。
        statusBar()->showMessage(QStringLiteral("STEP 导出成功：%1 · %2")
            .arg(path, QString::fromStdString(geometry.message)));
    } catch (const Standard_Failure& error) {
        QMessageBox::warning(this, QStringLiteral("STEP 导出失败"),
            QString::fromUtf8(error.GetMessageString() ? error.GetMessageString() : "几何内核异常"));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("STEP 导出失败"), QString::fromUtf8(error.what()));
    }
}


void MainWindow::on_actionImportStep_triggered()
{
    QFileDialog dialog(this, QStringLiteral("导入 STEP"));
    dialog.setObjectName(QStringLiteral("stepImportDialog"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter(QStringLiteral("STEP 文件 (*.step *.stp);;所有文件 (*)"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    const QString path = dialog.selectedFiles().first();
    const auto result = forge::infrastructure::StepIO::importShape(path);
    if (!result.success) {
        QMessageBox::warning(this, QStringLiteral("STEP 导入失败"), result.error);
        return;
    }
    try {
        auto& feature = document_.createImportedFeature(result.shape, QFileInfo(path).fileName().toStdString());
        selectedFeatureId_ = feature.id();
        refreshAfterHistoryChange();
        statusBar()->showMessage(QStringLiteral("STEP 导入成功：%1 · %2")
            .arg(path, QString::fromStdString(selectedFeatureId_)));
    } catch (const Standard_Failure& error) {
        QMessageBox::warning(this, QStringLiteral("STEP 导入失败"),
            QString::fromUtf8(error.GetMessageString() ? error.GetMessageString() : "几何内核异常"));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("STEP 导入失败"), QString::fromUtf8(error.what()));
    }
}
