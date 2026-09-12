// ============================================================
// MainWindow 实现（多特征 + 模型树版）
// ------------------------------------------------------------
// 本文件是"集合驱动界面"的样板：
//   · document_ 是唯一真相源；模型树只是它的展示
//   · 新建/修改统一经过 ModelingService，未来 AI 复用同一入口
//   · 点树 → selectFeature() 换选中 → 属性面板和 3D 视图跟着切
//   · 属性面板每行输入框变化 → setParameter → rebuild → showShapes（实时联动）
// ============================================================
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QVBoxLayout>          // 垂直布局（3D 视图/分组框占满用）
#include <QFormLayout>          // 表单布局（"标签 + 输入框"一行行排）
#include <QDialog>              // 新建对话框
#include <QDialogButtonBox>     // 对话框的 确定/取消 按钮组
#include <QPushButton>          // 确定/取消 按钮（改中文文字用）
#include <QDoubleSpinBox>       // 数字输入框
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QString>              // 字符串（中文标签/提示用）
#include <QStatusBar>           // 状态栏
#include <QStandardItemModel>   // 模型树的数据模型（含 QStandardItem）
#include <QTreeView>            // 模型树控件（clicked 信号）

#include "ui/Viewport3D.h"      // 3D 视图控件
#include "assistant/AgentController.h"
#include "domain/Feature.h"
#include "domain/FeatureCatalog.h"

#include <vector>               // std::vector
#include <map>
#include <string>               // std::string
#include <stdexcept>            // std::invalid_argument（捕获工厂的抛错）

namespace {

// ------------------------------------------------------------
// 参数英文名 → 中文标签（翻译是 UI 层的职责，domain 保持语言无关）
// ------------------------------------------------------------
QString paramLabel(const std::string& en) {
    if (en == "length") return QStringLiteral("长度");
    if (en == "width")  return QStringLiteral("宽度");
    if (en == "height") return QStringLiteral("高度");
    if (en == "radius") return QStringLiteral("半径");
    return QString::fromStdString(en);   // 还没翻译的新参数先显示原名
}

// ------------------------------------------------------------
// 类型名(domain 契约) → 中文名（菜单/对话框标题共用）
// ------------------------------------------------------------
QString kindLabel(const QString& kind) {
    if (kind == QStringLiteral("Box"))      return QStringLiteral("长方体");
    if (kind == QStringLiteral("Cylinder")) return QStringLiteral("圆柱体");
    if (kind == QStringLiteral("Sphere"))   return QStringLiteral("球体");
    return kind;   // 还没加中文名的类型先用原名
}

} // namespace

// ============================================================
// 构造函数
// ============================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , modelingService_(document_)
{
    ui->setupUi(this);          // 从 .ui 加载界面（含菜单、模型树、空属性舞台、3D 容器）

    // ---- 属性分组框内部整理：容器填满分组框、四边留 8px ----
    auto* groupLayout = new QVBoxLayout(ui->groupBox);
    groupLayout->setContentsMargins(8, 8, 8, 8);
    groupLayout->addWidget(ui->paramPanelContainer);

    // ---- 3D 视图嵌入右侧容器 ----
    auto* vlayout = new QVBoxLayout(ui->viewportContainer);
    vlayout->setContentsMargins(0, 0, 0, 0);
    viewport_ = new forge::ui::Viewport3D(ui->viewportContainer);
    vlayout->addWidget(viewport_);

    // ---- 模型树接线：QTreeView 需要"数据模型"才有内容 ----
    treeModel_ = new QStandardItemModel(this);   // 父对象 = this，Qt 自动释放
    ui->modelTree->setModel(treeModel_);
    // 用户点树的某一行 → 切到那一行对应的特征
    // 树现在是"分类文件夹 + 特征子项"结构，节点用 UserRole 保存稳定 Feature ID。
    connect(ui->modelTree, &QTreeView::clicked, this,
            [this](const QModelIndex& idx) {
                if (!idx.isValid()) return;
                const QVariant data = treeModel_->data(idx, Qt::UserRole);
                if (!data.isValid()) return;      // 点的是分类文件夹（没存下标）→ 忽略
                selectFeature(data.toString().toStdString());
            });

    // ---- 空文档启动：不预置任何特征 ----
    // 文档为空、selectedFeatureId_ 为空（没选中任何东西）。
    // 后面所有函数都会先按 ID 查找，空文档时安全跳过。
    rebuildFeatureTree();    // 空树（啥也没有，正常）
    rebuildParamPanel();     // 空面板（容器刚被清空）
    setupAssistantDock();
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

void MainWindow::on_actionUndo_triggered()
{
    if (!modelingService_.canUndo()) {
        statusBar()->showMessage(QStringLiteral("没有可以撤销的操作"));
        return;
    }

    modelingService_.undo();
    refreshAfterHistoryChange();
}

void MainWindow::on_actionRedo_triggered()
{
    if (!modelingService_.canRedo()) {
        statusBar()->showMessage(QStringLiteral("没有可以重做的操作"));
        return;
    }

    modelingService_.redo();
    refreshAfterHistoryChange();
}

// ============================================================
// createFeatureFromDialog：弹对话框 → 造特征 → 追加进集合并选中
// ============================================================
void MainWindow::createFeatureFromDialog(const QString& type)
{
    // ① 取出该类型要哪些参数（没有 → 类型不受支持，直接返回）
    const auto* descriptor = forge::domain::FeatureCatalog::find(type.toStdString());
    if (!descriptor) return;

    // ② 用代码现造对话框：标题 + 每个参数一行（标签+数字框）
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("新建") + kindLabel(type));   // "新建球体"…
    auto* form = new QFormLayout(&dlg);       // 表单布局：一行 = 标签 + 输入框
    std::vector<QDoubleSpinBox*> spins;       // 记下所有输入框，确定后好取值

    for (const auto& parameter : descriptor->parameters) {
        auto* spin = new QDoubleSpinBox(&dlg);
        spin->setRange(parameter.minimum, parameter.maximum);
        spin->setDecimals(1);
        spin->setValue(parameter.defaultValue);
        form->addRow(paramLabel(parameter.name), spin);
        spins.push_back(spin);
    }

    // ③ 底部放 确定/取消 按钮组，并把按钮文字改成中文
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                       | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
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
        parameters.emplace(descriptor->parameters[i].name, spins[i]->value());
    }

    try {
        auto& feature = modelingService_.createFeature(type.toStdString(), parameters);
        selectedFeatureId_ = feature.id();
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
    const auto& features = document_.features();
    for (int i = 0; i < static_cast<int>(features.size()); ++i) {
        const QString kind = QString::fromStdString(features[i]->name());  // "Box"/"Cylinder"…
        if (featuresByKind.find(kind) == featuresByKind.end()) {
            kindOrder.push_back(kind);       // 第一次见到这个类型 → 记下它的位置
        }
        featuresByKind[kind].push_back(i);
    }

    // ② 一个类型一个"文件夹"，特征作为它的子项
    QStandardItem* selectedTreeItem = nullptr;    // 记住选中项对应的树节点（用于恢复高亮）
    for (const QString& kind : kindOrder) {
        auto* cat = new QStandardItem(kindLabel(kind));   // 文件夹文字：中文类型名
        cat->setEditable(false);

        for (int i : featuresByKind[kind]) {
            auto* child = new QStandardItem(
                QString::fromStdString(features[i]->id()));   // 子项文字：id（如 Box002）
            child->setEditable(false);
            child->setData(QString::fromStdString(features[i]->id()), Qt::UserRole);
            if (features[i]->id() == selectedFeatureId_) selectedTreeItem = child;
            cat->appendRow(child);
        }
        treeModel_->appendRow(cat);            // 文件夹进树
    }

    // ③ 分类默认全展开 + 恢复选中高亮
    ui->modelTree->expandAll();
    if (selectedTreeItem) {
        ui->modelTree->setCurrentIndex(treeModel_->indexFromItem(selectedTreeItem));
    }
}

// ============================================================
// selectFeature：切换当前选中的特征
// ------------------------------------------------------------
// 树点击、新建后自动选中，都走这里：换稳定 ID → 面板重建 → 视图刷新
// ============================================================
void MainWindow::selectFeature(const std::string& id)
{
    if (!document_.findFeature(id)) return;
    selectedFeatureId_ = id;
    rebuildParamPanel();    // 面板换成这个特征的参数
    refreshViewport();      // 3D 换成这个特征的形状
}

// ============================================================
// rebuildParamPanel：按"选中特征"的参数重造属性面板
// ============================================================
void MainWindow::rebuildParamPanel()
{
    // ① 先清掉容器里上一次的输入框（布局和控件都要删）
    QLayout* oldLayout = ui->paramPanelContainer->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        delete oldLayout;
    }

    // ② 没有选中任何特征 → 面板留空
    auto* feature = document_.findFeature(selectedFeatureId_);
    if (!feature) return;
    const std::string featureId = feature->id();

    // ③ 按选中特征的真实参数生成输入框——parameters()（规矩①）驱动 UI！
    auto* form = new QFormLayout(ui->paramPanelContainer);
    form->setContentsMargins(12, 16, 12, 12);   // 上边距加大：别贴着"属性"标题
    form->setVerticalSpacing(10);               // 行距
    form->setHorizontalSpacing(12);             // 标签和输入框的间距

    const auto& params = feature->parameters();

    for (const auto& p : params) {
        auto* spin = new QDoubleSpinBox(ui->paramPanelContainer);
        spin->setDecimals(1);
        // 键盘输入“50”时，默认会先为字符“5”发出一次 valueChanged，
        // 再为最终值“50”发出第二次，导致 Undo 历史记录两个中间状态。
        // 关闭 keyboardTracking 后，键盘编辑只在回车或失去焦点时提交最终值；
        // 点击上下箭头仍会正常发出 valueChanged，并保持即时建模反馈。
        spin->setKeyboardTracking(false);
        if (const auto* descriptor = forge::domain::FeatureCatalog::findParameter(
                feature->name(), p.name())) {
            spin->setRange(descriptor->minimum, descriptor->maximum);
        }
        spin->setValue(p.asDouble());         // 初值 = 模型当前值
        form->addRow(paramLabel(p.name()), spin);

        // ★ 动态控件没有固定的 on_ 名字 → 手动 connect（lambda 捕获参数名）。
        //   箭头调整会立即提交；键盘输入则在编辑完成后只提交最终数值。
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, featureId, paramName = p.name()](double v) {
                    try {
                        modelingService_.setParameter(featureId, paramName, v);
                        refreshViewport();
                    } catch (const std::invalid_argument& e) {
                        statusBar()->showMessage(
                            QStringLiteral("修改失败: %1").arg(e.what()));
                    }
                });
    }
}

// ============================================================
// refreshViewport：重建全部形状 → 同时显示 → 高亮选中项 → 状态栏汇报
// ============================================================
void MainWindow::refreshViewport()
{
    if (!viewport_) return;                                      // 3D 视图还没就绪（启动早期）
    if (!document_.findFeature(selectedFeatureId_)) return;

    // ① 依次重建仓库里的所有 Feature。
    // validShapes 只收集成功生成的形状；selectedShapeIndex 记录当前选中项
    // 在“有效形状列表”里的位置，随后交给 Viewport3D 做高亮。
    std::vector<TopoDS_Shape> validShapes;
    const auto& features = document_.features();
    validShapes.reserve(features.size());
    int selectedShapeIndex = -1;

    for (int i = 0; i < static_cast<int>(features.size()); ++i) {
        TopoDS_Shape shape = features[i]->rebuild();  // 多态：三种 Feature 用同一个调用方式
        if (shape.IsNull()) {
            continue;                                  // 生成失败的空形状不交给显示层
        }

        if (features[i]->id() == selectedFeatureId_) {
            selectedShapeIndex = static_cast<int>(validShapes.size());
        }
        validShapes.push_back(shape);
    }

    // ② Viewport3D 只认识 TopoDS_Shape，不认识 Feature：继续保持业务与显示解耦。
    viewport_->showShapes(validShapes, selectedShapeIndex);

    // ③ 状态栏告诉用户当前选中谁，以及成功显示了多少个形状。
    statusBar()->showMessage(
        QStringLiteral("共 %1 个特征 · 已显示 %2 个 · 当前: %3 (%4)")
            .arg(features.size())
            .arg(validShapes.size())
            .arg(QString::fromStdString(document_.findFeature(selectedFeatureId_)->name()),
                 QString::fromStdString(selectedFeatureId_)));
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
        selectedFeatureId_ = features.empty() ? std::string() : features.back()->id();
    }

    rebuildFeatureTree();
    rebuildParamPanel();

    if (document_.features().empty()) {
        if (viewport_) {
            viewport_->showShapes({}, -1);
        }
        statusBar()->showMessage(QStringLiteral("文档为空"));
        return;
    }

    refreshViewport();
}

void MainWindow::setupAssistantDock()
{
    // 面板使用代码创建，暂时不修改 Designer 文件；后续 UI 定稿后可再迁回 .ui。
    // QDockWidget 允许用户拖动、停靠，且其父对象为主窗口，会随主窗口自动销毁。
    auto* dock = new QDockWidget(QStringLiteral("AI 建模助手"), this);
    dock->setObjectName(QStringLiteral("assistantDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // panel 是 Dock 内唯一的根控件；垂直布局把上方历史区和下方输入区组合起来。
    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);

    // 历史区只负责展示对话，不允许用户直接改写已经发送或返回的内容。
    auto* history = new QPlainTextEdit(panel);
    history->setReadOnly(true);
    history->setPlaceholderText(
        QStringLiteral("示例：创建一个长100、宽50、高30的长方体"));

    // 输入框占据剩余水平空间，发送按钮保持自身建议宽度。
    auto* inputRow = new QHBoxLayout();
    auto* input = new QLineEdit(panel);
    input->setPlaceholderText(QStringLiteral("描述你要创建或修改的模型…"));
    auto* sendButton = new QPushButton(QStringLiteral("发送"), panel);
    inputRow->addWidget(input, 1);
    inputRow->addWidget(sendButton);

    // 历史区的拉伸因子为 1，窗口变高时主要扩展对话显示空间。
    layout->addWidget(history, 1);
    layout->addLayout(inputRow);
    panel->setLayout(layout);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // AgentController 是 QObject 子对象，MainWindow 析构时由 Qt 自动释放。
    // 它引用的 document_ / modelingService_ 都是 MainWindow 成员，生命周期更长。
    agentController_ = new forge::assistant::AgentController(
        document_, modelingService_, this);

    // 点击按钮和按回车共用同一提交逻辑，避免两条入口出现行为差异。
    const auto submit = [this, input, history]() {
        const QString message = input->text().trimmed();
        // 忽略空白输入；请求进行中也禁止重入，避免两轮工具调用交叉修改文档。
        if (message.isEmpty() || agentController_->isBusy()) return;
        // 先把用户消息写入历史并清空输入，再异步交给 Agent 处理。
        history->appendPlainText(QStringLiteral("你：%1").arg(message));
        input->clear();
        agentController_->submit(message);
    };
    connect(sendButton, &QPushButton::clicked, this, submit);
    connect(input, &QLineEdit::returnPressed, this, submit);

    // 正常回答和错误使用不同前缀，让用户能快速区分模型回复与请求故障。
    connect(agentController_, &forge::assistant::AgentController::assistantMessage,
            this, [history](const QString& message) {
                history->appendPlainText(QStringLiteral("助手：%1").arg(message));
            });
    connect(agentController_, &forge::assistant::AgentController::errorMessage,
            this, [history](const QString& message) {
                history->appendPlainText(QStringLiteral("错误：%1").arg(message));
            });
    // 短暂的执行阶段提示放入状态栏，避免用技术细节污染对话历史。
    connect(agentController_, &forge::assistant::AgentController::statusMessage,
            this, [this](const QString& message) {
                statusBar()->showMessage(message);
            });
    // 网络请求和工具循环执行期间锁住输入，既给出视觉反馈，也形成第二层防重入保护。
    connect(agentController_, &forge::assistant::AgentController::busyChanged,
            this, [input, sendButton](bool busy) {
                input->setEnabled(!busy);
                sendButton->setEnabled(!busy);
            });
    // 工具真正修改模型后才刷新 UI；普通问答和查询不会触发不必要的 OCCT 重建。
    connect(agentController_, &forge::assistant::AgentController::modelChanged,
            this, [this](const QString& featureId) {
                // 选中新建或刚修改的特征，再同步重建树、参数表和三维视图。
                selectedFeatureId_ = featureId.toStdString();
                rebuildFeatureTree();
                rebuildParamPanel();
                refreshViewport();
            });
}
