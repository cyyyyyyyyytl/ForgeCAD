#ifndef ASSISTANTDIALOG_H
#define ASSISTANTDIALOG_H

#include <QDialog>
#include <QString>

namespace Ui {
class AssistantDialog;
}

class AssistantDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AssistantDialog(QWidget *parent = nullptr);
    ~AssistantDialog();

    void appendAssistantMessage(const QString& message);
    void appendErrorMessage(const QString& message);
    void setBusy(bool busy);

signals:
    void messageSubmitted(const QString& message);

private:
    void submitCurrentMessage();

    Ui::AssistantDialog *ui;
};

#endif // ASSISTANTDIALOG_H
