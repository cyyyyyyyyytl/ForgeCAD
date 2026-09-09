#include "assistant/DeepSeekClient.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace forge::assistant {

DeepSeekClient::DeepSeekClient(QObject* parent)
    : QObject(parent)
{
}

bool DeepSeekClient::hasApiKey() const
{
    return !qEnvironmentVariable("DEEPSEEK_API_KEY").trimmed().isEmpty();
}

void DeepSeekClient::send(const QJsonArray& messages, const QJsonArray& tools)
{
    // 每次请求时读取环境变量，便于从 IDE Run Configuration 注入密钥。
    // 不能把 API Key 缓存在配置文件，更不能在错误信息里输出 Authorization。
    const QString apiKey = qEnvironmentVariable("DEEPSEEK_API_KEY").trimmed();
    if (apiKey.isEmpty()) {
        emit requestFailed("未检测到 DEEPSEEK_API_KEY 环境变量");
        return;
    }

    QString endpoint = qEnvironmentVariable("DEEPSEEK_BASE_URL").trimmed();
    if (endpoint.isEmpty()) endpoint = "https://api.deepseek.com/chat/completions";

    QString model = qEnvironmentVariable("DEEPSEEK_MODEL").trimmed();
    if (model.isEmpty()) model = "deepseek-v4-flash";

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    request.setTransferTimeout(60000);

    // MVP 先关闭流式和 thinking：工具循环更简单，不需要拼接增量 tool_calls，
    // 也不需要在下一轮回传 reasoning_content。稳定后再单独扩展流式显示。
    const QJsonObject body{
        {"model", model},
        {"messages", messages},
        {"tools", tools},
        {"tool_choice", "auto"},
        {"stream", false},
        {"thinking", QJsonObject{{"type", "disabled"}}},
    };

    // post() 立即返回，finished 信号在网络完成后通过 Qt 事件循环触发。
    QNetworkReply* reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray payload = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError = reply->error();
        const QString networkErrorText = reply->errorString();
        reply->deleteLater();

        // HTTP 0 表示尚未得到服务器响应；此时 errorString() 比空响应体更有诊断价值。
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
            QString detail = QString::fromUtf8(payload.left(2000));
            if (detail.trimmed().isEmpty()) detail = networkErrorText;
            emit requestFailed(QString("DeepSeek 请求失败（HTTP %1，Qt错误 %2）：%3")
                                   .arg(status)
                                   .arg(static_cast<int>(networkError))
                                   .arg(detail));
            return;
        }
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit requestFailed("DeepSeek 返回了无法解析的 JSON");
            return;
        }
        emit responseReceived(document.object());
    });
}

} // namespace forge::assistant
