// ============================================================
// MainWindow 实现（多特征 + 模型树版）
// ------------------------------------------------------------
// 本文件是"集合驱动界面"的样板：
//   · features_（集合）是唯一真相源；模型树只是它的展示
//   · 新建特征 → 追加进集合 → 整树重建 → 自动选中新特征
//   · 点树 → selectFeature() 换选中 → 属性面板和 3D 视图跟着切
//   · 属性面板每行输入框变化 → setParameter → rebuild → showShape（实时联动）
// ============================================================
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QVBoxLayout>          // 垂直布局（3D 视图/分组框占满用）
#include <QFormLayout>          // 表单布局（"标签 + 输入框"一行行排）
#include <QDialog>              // 新建对话框
#include <QDialogButtonBox>     // 对话框的 确定/取消 按钮组
#include <QPushButton>          // 确定/取消 按钮（改中文文字用）
#include <QDoubleSpinBox>       // 数字输入框
#include <QString>              // 字符串（中文标签/提示用）
#include <QStatusBar>           // 状态栏
#include <QStandardItemModel>   // 模型树的数据模型（含 QStandardItem）
#include <QTreeView>            // 模型树控件（clicked 信号）

#include "ui/Viewport3D.h"      // 3D 视图控件
#include "domain/FeatureFactory.h"  // 工厂：造任意特征（含 Feature 完整定义）

#include <vector>               // std::vector
#include <map>                  // std::map（每种类型的编号计数器用）
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

// ------------------------------------------------------------
// 每种类型在建"新建对话框"时要哪些参数、默认值是多少
// （必须和 FeatureFactory 的契约一致：Box=3 个数，Cylinder=2 个数，Sphere=1 个数）
// ------------------------------------------------------------
struct ParamDef {
    const char* name;      // 参数名（与 domain 一致）
    double defValue;       // 对话框初值
};

const std::vector<ParamDef>& defsForType(const QString& type) {
    static const std::vector<ParamDef> boxDefs = {
        {"length", 100.0}, {"width", 50.0}, {"height", 30.0}
    };
    static const std::vector<ParamDef> cylDefs = {
        {"radius", 20.0}, {"height", 60.0}
    };
    static const std::vector<ParamDef> sphDefs = {
        {"radius", 20.0}
    };
    static const std::vector<ParamDef> emptyDefs;   // 未知类型：空清单
    if (type == QStringLiteral("Box"))      return boxDefs;
    if (type == QStringLiteral("Cylinder")) return cylDefs;
    if (type == QStringLiteral("Sphere"))   return sphDefs;
    return emptyDefs;
}

} // namespace

// ============================================================
// 构造函数
// ============================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
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
    // 树现在是"分类文件夹 + 特征子项"结构，行号 ≠ 集合下标，
    // 所以点击时从节点的数据里取"集合下标"（建树时用 UserRole 存进去的）
    connect(ui->modelTree, &QTreeView::clicked, this,
            [this](const QModelIndex& idx) {
                if (!idx.isValid()) return;
                const QVariant data = treeModel_->data(idx, Qt::UserRole);
                if (!data.isValid()) return;      // 点的是分类文件夹（没存下标）→ 忽略
                selectFeature(data.toInt());
            });

    // ---- 空文档启动：不预置任何特征 ----
    // 集合为空、selectedIndex_ = -1（没选中任何东西）。
    // 后面所有函数都带"越界防御"（先查下标再动手），
    // 空集合时它们会安全跳过：树是空的、面板留空、视图不出图，不会崩。
    rebuildFeatureTree();    // 空树（啥也没有，正常）
    rebuildParamPanel();     // 空面板（容器刚被清空）
    statusBar()->showMessage(QStringLiteral("用菜单\"新建\"添加你的第一个特征"));
    // 注意：这里不再 QTimer 首图——没有特征可显示；等用户新建第一个时
    // createFeatureFromDialog 会自己刷新视图（那时窗口早已显示、视图已就绪）。
}

MainWindow::~MainWindow()
{
    delete ui;   // 只 delete ui；其余成员：viewport_/treeModel_ 有父控件（Qt 管），features_ 是 unique_ptr 自动释放
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

// ============================================================
// createFeatureFromDialog：弹对话框 → 造特征 → 追加进集合并选中
// ============================================================
void MainWindow::createFeatureFromDialog(const QString& type)
{
    // ① 取出该类型要哪些参数（没有 → 类型不受支持，直接返回）
    const std::vector<ParamDef>& defs = defsForType(type);
    if (defs.empty()) return;

    // ② 用代码现造对话框：标题 + 每个参数一行（标签+数字框）
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("新建") + kindLabel(type));   // "新建球体"…
    auto* form = new QFormLayout(&dlg);       // 表单布局：一行 = 标签 + 输入框
    std::vector<QDoubleSpinBox*> spins;       // 记下所有输入框，确定后好取值

    for (const ParamDef& d : defs) {
        auto* spin = new QDoubleSpinBox(&dlg);
        spin->setRange(1.0, 10000.0);   // 前置校验：输不出非法值
        spin->setDecimals(1);
        spin->setValue(d.defValue);     // 初值 = 默认值
        form->addRow(paramLabel(d.name), spin);   // addRow(标签文字, 输入框)
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
    std::vector<double> sizes;
    for (auto* s : spins) sizes.push_back(s->value());

    try {
        features_.push_back(forge::domain::FeatureFactory::create(
            type.toStdString(), nextFeatureId(type).toStdString(), sizes));
        selectedIndex_ = static_cast<int>(features_.size()) - 1;  // 新特征自动选中
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
// nextFeatureId：生成唯一身份证（每种类型自己的编号，互不串号）
// ------------------------------------------------------------
// 用"类型 → 计数器"的 map：Box 从 Box001 开始，第二个盒子是 Box002；
// Cylinder/Sphere 各自从 001 开始。之前用全局流水号（Box004）会让人困惑。
// ============================================================
QString MainWindow::nextFeatureId(const QString& type)
{
    static std::map<QString, int> seqByType;      // 静态：进程内共享；每种类型独立计数
    return type + QString("%1").arg(++seqByType[type], 3, 10, QChar('0'));
    // ++map[key]：第一次访问该类型自动从 0 开始 → 自增为 1 → "Box001"
}

// ============================================================
// rebuildFeatureTree：用 features_ 整树重建（按类型分组）
// ------------------------------------------------------------
// 结构：
//   长方体                 ← 分类"文件夹"（文字 = 中文类型名）
//    ├─ Box001             ← 特征子项（文字 = id；节点里存集合下标 UserRole）
//    └─ Box002
//   圆柱体
//    └─ Cylinder001
// 集合是唯一真相源，树只是展示：每次集合变了就清空重建，并恢复选中高亮。
// 点击子项时从 UserRole 取出集合下标 → selectFeature。
// ============================================================
void MainWindow::rebuildFeatureTree()
{
    treeModel_->clear();                      // 清空旧树

    // ① 按"首次出现顺序"把特征归类：kind -> 属于它的集合下标们
    std::vector<QString> kindOrder;                          // 分类顺序（如 长方体、圆柱体…）
    std::map<QString, std::vector<int>> featuresByKind;      // 类型名 -> 下标列表
    for (int i = 0; i < static_cast<int>(features_.size()); ++i) {
        const QString kind = QString::fromStdString(features_[i]->name());  // "Box"/"Cylinder"…
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
                QString::fromStdString(features_[i]->id()));   // 子项文字：id（如 Box002）
            child->setEditable(false);
            child->setData(i, Qt::UserRole);   // ★把集合下标存进节点（点击时取出来）
            if (i == selectedIndex_) selectedTreeItem = child;   // 当前选中的 → 记住它
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
// 树点击、新建后自动选中，都走这里：换下标 → 面板重建 → 视图刷新
// ============================================================
void MainWindow::selectFeature(int index)
{
    if (index < 0 || index >= static_cast<int>(features_.size())) return;  // 越界防御
    selectedIndex_ = index;
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
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(features_.size())) return;
    const int feaIndex = selectedIndex_;   // 记下来，lambda 里用（此时它不会变）

    // ③ 按选中特征的真实参数生成输入框——parameters()（规矩①）驱动 UI！
    auto* form = new QFormLayout(ui->paramPanelContainer);
    form->setContentsMargins(12, 16, 12, 12);   // 上边距加大：别贴着"属性"标题
    form->setVerticalSpacing(10);               // 行距
    form->setHorizontalSpacing(12);             // 标签和输入框的间距

    const auto& params = features_[feaIndex]->parameters();   // const 引用，避免拷贝

    for (const auto& p : params) {
        auto* spin = new QDoubleSpinBox(ui->paramPanelContainer);
        spin->setDecimals(1);
        spin->setRange(1.0, 10000.0);         // 前置校验：输不出非法值
        spin->setValue(p.asDouble());         // 初值 = 模型当前值
        form->addRow(paramLabel(p.name()), spin);

        // ★ 动态控件没有固定的 on_ 名字 → 手动 connect（lambda 捕获参数名）
        //   值一变：写进"当时选中"的特征 → 立刻重建显示
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, feaIndex, paramName = p.name()](double v) {
                    features_[feaIndex]->setParameter(paramName, v);
                    refreshViewport();
                });
    }
}

// ============================================================
// refreshViewport：选中特征 → 重建形状 → 显示 → 状态栏汇报
// ============================================================
void MainWindow::refreshViewport()
{
    if (!viewport_) return;                                      // 3D 视图还没就绪（启动早期）
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(features_.size())) return;

    TopoDS_Shape shape = features_[selectedIndex_]->rebuild();   // 多态规矩④：不管哪种类型同样调用

    if (!shape.IsNull()) {
        viewport_->showShape(shape);
        statusBar()->showMessage(
            QStringLiteral("共 %1 个特征 · 当前: %2 (%3)")
                .arg(features_.size())
                .arg(QString::fromStdString(features_[selectedIndex_]->name()),
                     QString::fromStdString(features_[selectedIndex_]->id())));
    }
}
