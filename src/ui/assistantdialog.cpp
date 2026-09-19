#include "assistantdialog.h"     // 本类的手写声明。
#include "ui_assistantdialog.h"  // uic 从 .ui 自动生成，只引用、不手工修改。

// 构造函数先初始化 QDialog 基类，再在堆上创建 Ui 包装对象。
AssistantDialog::AssistantDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AssistantDialog)
{
    // setupUi 根据 .ui 描述创建输入框、记录框和按钮，并建立 Qt 父子关系。
    ui->setupUi(this);

    // Designer 负责创建控件；这里仅连接“点击发送”和“按回车”两种提交入口。
    // 第一条连接：发送按钮发出 clicked 信号时，调用统一提交函数。
    connect(ui->assistantSendButton, &QPushButton::clicked,
            this, &AssistantDialog::submitCurrentMessage);
    // 第二条连接：单行输入框收到回车时，也调用同一个提交函数。
    connect(ui->assistantInput, &QLineEdit::returnPressed,
            this, &AssistantDialog::submitCurrentMessage);
}

// Ui 包装对象由 new 创建，因此由本类在析构时配对 delete。
AssistantDialog::~AssistantDialog()
{
    // setupUi 创建的控件已经以 this 为父对象，会由 Qt 自动释放；这里只删包装对象。
    delete ui;
}

// 收集一条用户输入，并把它转换成 messageSubmitted 信号交给外层控制器。
void AssistantDialog::submitCurrentMessage()
{
    // text() 取得输入框文字；trimmed() 去掉首尾空白，避免把纯空格当成消息。
    const QString message = ui->assistantInput->text().trimmed();
    // 空消息没有意义；输入框被禁用则说明上一轮请求尚未结束，也不允许重复发送。
    if (message.isEmpty() || !ui->assistantInput->isEnabled()) return;

    // 先把用户原话显示在聊天记录中，使异步网络等待期间仍有即时反馈。
    ui->assistantHistory->appendPlainText(QStringLiteral("你：%1").arg(message));
    // 提交成功后清空输入框，为下一条消息做准备。
    ui->assistantInput->clear();
    // 发出 Qt 信号；MainWindow 已把它连接到 AgentController::submit。
    emit messageSubmitted(message);
}

// 正常回答由 AgentController 的 assistantMessage 信号调用到这里。
void AssistantDialog::appendAssistantMessage(const QString& message)
{
    // 添加固定前缀，让同一个纯文本记录框中的说话方清晰可见。
    ui->assistantHistory->appendPlainText(QStringLiteral("助手：%1").arg(message));
}

// 错误消息使用独立前缀，避免用户把网络错误误认为模型的自然语言回答。
void AssistantDialog::appendErrorMessage(const QString& message)
{
    // appendPlainText 会在末尾新增一段文字，不会覆盖之前的聊天历史。
    ui->assistantHistory->appendPlainText(QStringLiteral("错误：%1").arg(message));
}

// busy=true 表示一次 Agent 循环正在进行，false 表示用户可以继续输入。
void AssistantDialog::setBusy(bool busy)
{
    // setEnabled 接收“是否允许操作”，因此要对 busy 取反。
    ui->assistantInput->setEnabled(!busy);
    // 按钮和输入框保持相同状态，堵住鼠标点击和键盘回车两种重复提交入口。
    ui->assistantSendButton->setEnabled(!busy);
}
