// ============================================================
// MainWindow 实现（动态面板版）
// ------------------------------------------------------------
// 本文件是"数据驱动界面"的样板：
//   · 创建特征：菜单 → 对话框(按类型现造输入框) → FeatureFactory → current_
//   · 属性面板：每次 current_ 变了 → 按 current_->parameters() 重造输入框
//   · 任何输入框变化 → setParameter → rebuild → showShape（实时联动）
// 所有"参数英文名 → 中文标签"的翻译都收在本文件（UI 层的职责），
// domain 层保持语言无关。
// ============================================================
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QVBoxLayout>          // 垂直布局（3D 视图占满容器用）
#include <QFormLayout>          // 表单布局（"标签 + 输入框"一行行排）
#include <QDialog>              // 新建对话框
#include <QDialogButtonBox>     // 对话框的 确定/取消 按钮组
#include <QPushButton>          // 确定/取消 按钮（改中文文字用）
#include <QDoubleSpinBox>       // 数字输入框
#include <QString>              // 字符串（中文标签/提示用）
#include <QTimer>               // 延迟首次显示（等 3D 视图就绪）
#include <QStatusBar>           // 状态栏

#include "ui/Viewport3D.h"      // 3D 视图控件
#include "domain/FeatureFactory.h"  // 工厂：造任意特征（含 Feature 完整定义）

#include <vector>               // std::vector
#include <string>               // std::string
#include <stdexcept>            // std::invalid_argument（捕获工厂的抛错）

namespace {

// ------------------------------------------------------------
// 参数英文名 → 中文标签
// ------------------------------------------------------------
// 为什么翻译放在 UI 层：domain 的参数名是语言无关的契约（"length"），
//   界面显示成什么语言是界面的事。将来换英文界面，只改这一处。
QString paramLabel(const std::string& en) {
    if (en == "length") return QStringLiteral("长度");
    if (en == "width")  return QStringLiteral("宽度");
    if (en == "height") return QStringLiteral("高度");
    if (en == "radius") return QStringLiteral("半径");
    return QString::fromStdString(en);   // 还没翻译的新参数先显示原名
}

// ------------------------------------------------------------
// 类型名(domain 契约) → 中文名
// ------------------------------------------------------------
// 菜单项文字、对话框标题共用这一处；加新类型 = 在这里加一行
QString kindLabel(const QString& kind) {
    if (kind == QStringLiteral("Box"))      return QStringLiteral("长方体");
    if (kind == QStringLiteral("Cylinder")) return QStringLiteral("圆柱体");
    if (kind == QStringLiteral("Sphere"))   return QStringLiteral("球体");
    return kind;   // 还没加中文名的类型先用原名
}

// ------------------------------------------------------------
// 每种类型在建"新建对话框"时要哪些参数、默认值是多少
// ------------------------------------------------------------
// 这份表必须和 FeatureFactory 的契约一致（Box=3 个数，Cylinder=2 个数，Sphere=1 个数）
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
    ui->setupUi(this);          // 从 .ui 加载界面（含菜单、空属性舞台、3D 容器）

    // ---- 属性分组框内部整理 ----
    // .ui 里 paramPanelContainer 是绝对定位的（旧做法）；给它套一层垂直布局，
    // 容器就自动填满分组框内部、四边留 8px——窗口怎么变都跟着分组框走
    auto* groupLayout = new QVBoxLayout(ui->groupBox);
    groupLayout->setContentsMargins(8, 8, 8, 8);
    groupLayout->addWidget(ui->paramPanelContainer);

    // ---- 3D 视图嵌入右侧容器（原样保留）----
    auto* vlayout = new QVBoxLayout(ui->viewportContainer);   // 铺垂直布局
    vlayout->setContentsMargins(0, 0, 0, 0);
    viewport_ = new forge::ui::Viewport3D(ui->viewportContainer);
    vlayout->addWidget(viewport_);

    // ---- 启动默认模型：工厂造一个长方体（多态持有为 Feature）----
    // 尺寸与 FeatureFactory 契约一致；用 try 兜底：万一工厂抛错不崩程序
    try {
        current_ = forge::domain::FeatureFactory::create(
            "Box", "Box001", {100.0, 50.0, 30.0});
    } catch (const std::invalid_argument& e) {
        statusBar()->showMessage(QString("启动模型创建失败: %1").arg(e.what()));
    }

    // 属性面板按当前特征动态生成（此刻窗口未显示、视图未就绪，先不出图）
    rebuildParamPanel();

    // 首次出图：延迟到事件循环启动、3D 视图初始化（showEvent 已跑）之后
    QTimer::singleShot(0, this, [this] { refreshViewport(); });
}

MainWindow::~MainWindow()
{
    delete ui;   // 只 delete ui；viewport_ 有父控件（Qt 会销毁），current_ 是 unique_ptr 自动释放
}

// ============================================================
// 槽：菜单"新建→长方体 / 圆柱体"
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
// createFeatureFromDialog：弹出"填参数"对话框 → 造特征 → 接管当前模型
// ============================================================
void MainWindow::createFeatureFromDialog(const QString& type)
{
    // ① 取出该类型要哪些参数（没有 → 类型不受支持，直接返回）
    const std::vector<ParamDef>& defs = defsForType(type);
    if (defs.empty()) return;

    // ② 用代码现造对话框：标题 + 每个参数一行（标签+数字框）
    QDialog dlg(this);
    // 标题用"中文类型名"拼：加新类型只改 kindLabel 一处，不再写死三元表达式
    dlg.setWindowTitle(QStringLiteral("新建") + kindLabel(type));
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

    // ⑤ 收集数值 → 工厂造特征（参数个数不对/未知类型会抛，捕获并提示）
    std::vector<double> sizes;
    for (auto* s : spins) sizes.push_back(s->value());

    try {
        current_ = forge::domain::FeatureFactory::create(
            type.toStdString(), nextFeatureId(type).toStdString(), sizes);
    } catch (const std::invalid_argument& e) {
        statusBar()->showMessage(QStringLiteral("创建失败: %1").arg(e.what()));
        return;
    }

    // ⑥ 面板跟着新特征变 + 立刻出图（此刻窗口早已显示，视图已就绪）
    rebuildParamPanel();
    refreshViewport();
}

// ============================================================
// nextFeatureId：生成唯一身份证（进程内递增，保证不重号）
// ============================================================
QString MainWindow::nextFeatureId(const QString& type)
{
    static int seq = 0;                        // 静态变量：整个进程共享，只初始化一次
    return type + QString("%1").arg(++seq, 3, 10, QChar('0'));  // Box001、Cylinder002…
}

// ============================================================
// rebuildParamPanel：按当前特征的参数"重造"属性面板
// ============================================================
void MainWindow::rebuildParamPanel()
{
    // ① 先清掉容器里上一次的输入框（布局和控件都要删）
    //    takeAt 逐个取出 → 控件 deleteLater、条目 delete
    QLayout* oldLayout = ui->paramPanelContainer->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget()) w->deleteLater();
            delete item;
        }
        delete oldLayout;
    }

    if (!current_) return;                    // 没有当前模型 → 面板留空

    // ② 按当前特征的真实参数生成输入框——parameters()（规矩①）驱动 UI！
    auto* form = new QFormLayout(ui->paramPanelContainer);
    // 留白：内容不顶到容器顶/边，行与行之间拉开距离（外观归外观，逻辑归逻辑）
    form->setContentsMargins(12, 16, 12, 12);   // 上边距加大：别贴着"属性"标题
    form->setVerticalSpacing(10);               // 行距：长/宽/高 之间别挤在一起
    form->setHorizontalSpacing(12);             // 标签和输入框之间的间距
    const auto& params = current_->parameters();   // const 引用，避免整个 vector 的拷贝

    for (const auto& p : params) {
        auto* spin = new QDoubleSpinBox(ui->paramPanelContainer);
        spin->setDecimals(1);
        spin->setRange(1.0, 10000.0);         // 前置校验：输不出非法值
        spin->setValue(p.asDouble());         // 初值 = 模型当前值
        form->addRow(paramLabel(p.name()), spin);

        // ★ 动态控件没有固定的 on_ 名字 → 改用手动 connect。
        //   lambda 捕获参数名（C++14 初始化捕获：把名字"拷一份"进闭包），
        //   值一变：写进模型 → 立刻重建显示。这就是"实时联动"。
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, paramName = p.name()](double v) {
                    current_->setParameter(paramName, v);
                    refreshViewport();
                });
    }
}

// ============================================================
// refreshViewport：当前特征 → 重建形状 → 显示 → 状态栏汇报
// ============================================================
void MainWindow::refreshViewport()
{
    if (!current_ || !viewport_) return;      // 防御：模型或视图不存在就跳过

    TopoDS_Shape shape = current_->rebuild(); // 多态规矩④：不管 Box 还是 Cylinder，同样调用

    if (!shape.IsNull()) {
        viewport_->showShape(shape);
        statusBar()->showMessage(
            QStringLiteral("当前模型: %1 (%2)")
                .arg(QString::fromStdString(current_->name()),
                     QString::fromStdString(current_->id())));
    }
}
