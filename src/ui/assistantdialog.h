// 传统头文件保护宏：防止同一个头文件在一次编译中被重复展开。
#ifndef ASSISTANTDIALOG_H
#define ASSISTANTDIALOG_H

#include <QDialog>  // QDialog：可独立显示的 Qt 对话框基类。
#include <QString>  // QString：Qt 使用的 Unicode 字符串类型。

namespace Ui {
// 这里只做前向声明；真正的 Ui::AssistantDialog 由 Qt 的 uic 根据
// assistantdialog.ui 自动生成，因此手写代码不需要也不应该复制它的定义。
class AssistantDialog;
}

// AssistantDialog 是 AI 聊天窗口的手写控制类；可见控件由 .ui 文件创建，
// 这个类只负责读取输入、发出消息和更新聊天记录。
class AssistantDialog : public QDialog
{
    // Q_OBJECT 让本类拥有 Qt 元对象信息，从而可以声明并发出 signals。
    Q_OBJECT

public:
    // parent 建立 Qt 父子关系；父窗口销毁时会自动销毁本对话框。
    explicit AssistantDialog(QWidget *parent = nullptr);
    // 析构函数负责释放手写持有的 Ui 包装对象；其中的子控件由 Qt 父子机制释放。
    ~AssistantDialog();

    // 将模型的正常回答追加到只读聊天记录中。
    void appendAssistantMessage(const QString& message);
    // 将网络或协议错误以“错误：”前缀追加到聊天记录中。
    void appendErrorMessage(const QString& message);
    // 请求进行中禁用输入和发送，结束后重新启用，避免并发提交两条消息。
    void setBusy(bool busy);

signals:
    // 用户完成一次有效输入时发出；信号只传递文字，不直接访问网络层。
    void messageSubmitted(const QString& message);

private:
    // 点击发送按钮和按回车最终都调用这里，保证两种入口执行完全相同的校验。
    void submitCurrentMessage();

    // 指向 uic 自动生成的界面包装对象，从它可以取得 assistantInput 等控件。
    Ui::AssistantDialog *ui;
};

#endif // ASSISTANTDIALOG_H：头文件保护结束。
