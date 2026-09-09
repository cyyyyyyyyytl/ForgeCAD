#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>

namespace forge::assistant {

// ============================================================
// DeepSeekClient：模型服务适配器
// ------------------------------------------------------------
// 只负责请求序列化、Authorization、超时和响应解析，不执行任何建模工具。
// QNetworkAccessManager 是异步的，请求期间 Qt GUI 事件循环仍可正常刷新。
// 密钥只从进程环境读取，绝不进入源码、日志和 Git。
// ============================================================
class DeepSeekClient : public QObject {
    Q_OBJECT

public:
    explicit DeepSeekClient(QObject* parent = nullptr);

    void send(const QJsonArray& messages, const QJsonArray& tools);
    bool hasApiKey() const;

signals:
    void responseReceived(const QJsonObject& response);
    void requestFailed(const QString& error);

private:
    QNetworkAccessManager network_;
};

} // namespace forge::assistant
