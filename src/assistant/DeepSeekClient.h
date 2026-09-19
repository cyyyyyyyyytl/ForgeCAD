#pragma once // 保证头文件在一次编译中只展开一次。

#include <QJsonArray>             // 保存 messages 和 tools 两个 JSON 数组。
#include <QJsonObject>            // 信号返回解析完成的 JSON 根对象。
#include <QNetworkAccessManager>  // Qt 的异步 HTTP 请求管理器。
#include <QObject>                // QObject 提供父子生命周期和信号槽机制。

namespace forge::assistant {

// ============================================================
// DeepSeekClient：模型服务适配器
// ------------------------------------------------------------
// 只负责请求序列化、Authorization、超时和响应解析，不执行任何建模工具。
// 它不知道 Feature、ModelDocument 和工具含义，因此更换模型供应商时不会
// 影响数据层；反过来增加新 Feature 也不需要修改网络代码。
// QNetworkAccessManager 是异步的，请求期间 Qt GUI 事件循环仍可正常刷新。
// 密钥只从进程环境读取，绝不进入源码、日志和 Git。
// ============================================================
class DeepSeekClient : public QObject {
    // Q_OBJECT 启用 moc 生成的元对象代码，使本类可以发出下面两个信号。
    Q_OBJECT

public:
    // QObject parent 只负责 Client 自身生命周期；network_ 是值成员自动释放。
    explicit DeepSeekClient(QObject* parent = nullptr);

    // 发送一轮完整 Chat Completions 请求；结果通过下面两个信号异步返回。
    void send(const QJsonArray& messages, const QJsonArray& tools);
    bool hasApiKey() const; // 只检查环境变量是否存在，不读取到 UI 或日志。

signals:
    void responseReceived(const QJsonObject& response); // 收到合法 2xx JSON 对象。
    void requestFailed(const QString& error);           // 网络、HTTP 或 JSON 解析失败。

private:
    QNetworkAccessManager network_; // Qt 异步网络管理器，必须在所属线程使用。
};

} // namespace forge::assistant
