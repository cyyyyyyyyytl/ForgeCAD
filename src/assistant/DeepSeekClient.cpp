#include "assistant/DeepSeekClient.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace forge::assistant {

// QNetworkAccessManager 作为成员随 Client 构造，在同一 Qt 线程中处理全部请求。
// parent 只设置给 DeepSeekClient 自身；network_ 是值成员，会自动析构。
DeepSeekClient::DeepSeekClient(QObject* parent)
    : QObject(parent)
{
}

// 只判断环境变量是否存在，不返回、更不打印密钥内容。
// 可供未来设置页面在发送前显示“已配置/未配置”状态。
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

    // BASE_URL 和 MODEL 可覆盖，便于切换兼容服务；未设置时使用项目默认值。
    QString endpoint = qEnvironmentVariable("DEEPSEEK_BASE_URL").trimmed();
    if (endpoint.isEmpty()) endpoint = "https://api.deepseek.com/chat/completions";

    QString model = qEnvironmentVariable("DEEPSEEK_MODEL").trimmed();
    if (model.isEmpty()) model = "deepseek-v4-flash";

    // 构造 HTTP 请求：JSON Content-Type + Bearer 认证 + 60 秒超时。
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
    // 以 this 作为 connect 上下文：若 Client 提前销毁，Qt 会自动断开回调，
    // 避免 finished 到来时 lambda 访问已经释放的 Agent 对象。
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        // finished 后先一次性读取响应体和状态，再安排 reply 安全延迟销毁。
        const QByteArray payload = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError = reply->error();
        const QString networkErrorText = reply->errorString();
        reply->deleteLater();

        // HTTP 0 表示尚未得到服务器响应；此时 errorString() 比空响应体更有诊断价值。
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        // 网络错误与非 2xx HTTP 都走失败信号；响应体最多展示 2000 字节，
        // 防止服务端异常页面无限灌入 UI，同时不会包含本地 Authorization 头。
        if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
            QString detail = QString::fromUtf8(payload.left(2000));
            if (detail.trimmed().isEmpty()) detail = networkErrorText;
            emit requestFailed(QString("DeepSeek 请求失败（HTTP %1，Qt错误 %2）：%3")
                                   .arg(status)
                                   .arg(static_cast<int>(networkError))
                                   .arg(detail));
            return;
        }
        // 成功状态也不能盲信响应体，只有合法 JSON object 才交给 AgentController。
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit requestFailed("DeepSeek 返回了无法解析的 JSON");
            return;
        }
        // 网络层只发出原始响应对象，choices/tool_calls 的业务解释由 Agent 层负责。
        emit responseReceived(document.object());
    });
}

} // namespace forge::assistant
