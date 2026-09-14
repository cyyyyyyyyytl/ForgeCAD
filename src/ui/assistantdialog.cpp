#include "assistantdialog.h"
#include "ui_assistantdialog.h"

AssistantDialog::AssistantDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AssistantDialog)
{
    ui->setupUi(this);

    // Designer 负责创建控件；这里仅连接“点击发送”和“按回车”两种提交入口。
    connect(ui->assistantSendButton, &QPushButton::clicked,
            this, &AssistantDialog::submitCurrentMessage);
    connect(ui->assistantInput, &QLineEdit::returnPressed,
            this, &AssistantDialog::submitCurrentMessage);
}

AssistantDialog::~AssistantDialog()
{
    delete ui;
}

void AssistantDialog::submitCurrentMessage()
{
    const QString message = ui->assistantInput->text().trimmed();
    if (message.isEmpty() || !ui->assistantInput->isEnabled()) return;

    ui->assistantHistory->appendPlainText(QStringLiteral("你：%1").arg(message));
    ui->assistantInput->clear();
    emit messageSubmitted(message);
}

void AssistantDialog::appendAssistantMessage(const QString& message)
{
    ui->assistantHistory->appendPlainText(QStringLiteral("助手：%1").arg(message));
}

void AssistantDialog::appendErrorMessage(const QString& message)
{
    ui->assistantHistory->appendPlainText(QStringLiteral("错误：%1").arg(message));
}

void AssistantDialog::setBusy(bool busy)
{
    ui->assistantInput->setEnabled(!busy);
    ui->assistantSendButton->setEnabled(!busy);
}
