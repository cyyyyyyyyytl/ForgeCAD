#include "assistant/DeepSeekClient.h" // 本类声明以及 Qt 网络成员类型。

#include <QJsonDocument>  // JSON 对象与 HTTP 请求/响应字节之间的转换。
#include <QNetworkReply>  // 表示一个仍在进行或已经完成的异步网络响应。
#include <QNetworkRequest>// 保存 URL、请求头和超时等请求配置。
#include <QUrl>           // 对服务端地址进行结构化封装和校验。

namespace forge::assistant {

// QNetworkAccessManager 作为成员随 Client 构造，在同一 Qt 线程中处理全部请求。
// parent 只设置给 DeepSeekClient 自身；network_ 是值成员，会自动析构。
DeepSeekClient::DeepSeekClient(QObject* parent)
    : QObject(parent) // 把 Client 加入 parent 的 Qt 对象树，父对象销毁时自动销毁它。
{
    // network_ 是值成员，会自动构造，因此构造函数体不需要额外初始化。
}

// 只判断环境变量是否存在，不返回、更不打印密钥内容。
// 可供未来设置页面在发送前显示“已配置/未配置”状态。
bool DeepSeekClient::hasApiKey() const
{
    // qEnvironmentVariable 只读取当前进程环境；trimmed 防止纯空格被误判为有效密钥。
    return !qEnvironmentVariable("DEEPSEEK_API_KEY").trimmed().isEmpty();
}

// 将一轮完整对话和工具说明序列化为请求，并异步等待模型响应。
void DeepSeekClient::send(const QJsonArray& messages, const QJsonArray& tools)
{
    // 每次请求时读取环境变量，便于从 IDE Run Configuration 注入密钥。
    // 不能把 API Key 缓存在配置文件，更不能在错误信息里输出 Authorization。
    const QString apiKey = qEnvironmentVariable("DEEPSEEK_API_KEY").trimmed();
    if (apiKey.isEmpty()) {
        emit requestFailed("未检测到 DEEPSEEK_API_KEY 环境变量"); // 让上层显示可理解错误。
        return; // 没有认证信息时绝不发送一个必然失败的网络请求。
    }

    // BASE_URL 和 MODEL 可覆盖，便于切换兼容服务；未设置时使用项目默认值。
    QString endpoint = qEnvironmentVariable("DEEPSEEK_BASE_URL").trimmed();
    if (endpoint.isEmpty()) {
        // 未配置自定义兼容服务时，使用项目约定的 DeepSeek Chat Completions 地址。
        endpoint = "https://api.deepseek.com/chat/completions";
    }

    QString model = qEnvironmentVariable("DEEPSEEK_MODEL").trimmed();
    if (model.isEmpty()) {
        // 环境变量允许部署时切换模型；源码默认值保证普通运行配置足够简洁。
        model = "deepseek-v4-flash";
    }

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
    // Compact JSON 不带缩进和换行，网络负载更小；reply 代表仍在进行的异步请求。
    QNetworkReply* reply = network_.post(
        request,
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    // 以 this 作为 connect 上下文：若 Client 提前销毁，Qt 会自动断开回调，
    // 避免 finished 到来时 lambda 访问已经释放的 Agent 对象。
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        // finished 后先一次性读取响应体和状态，再安排 reply 安全延迟销毁。
        const QByteArray payload = reply->readAll(); // 一次性取出服务器返回的全部响应体。
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt(); // 读取 HTTP 状态码。
        const auto networkError = reply->error();              // 保存 Qt 网络层错误枚举。
        const QString networkErrorText = reply->errorString(); // 保存可供用户诊断的错误文字。
        reply->deleteLater(); // 当前位于 reply 信号回调中，用延迟删除避免立即析构发送者。

        // HTTP 0 表示尚未得到服务器响应；此时 errorString() 比空响应体更有诊断价值。
        QJsonParseError parseError; // 接收 JSON 解析是否成功以及失败位置等状态。
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        // 网络错误与非 2xx HTTP 都走失败信号；响应体最多展示 2000 字节，
        // 防止服务端异常页面无限灌入 UI，同时不会包含本地 Authorization 头。
        if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
            // 服务端响应可能很大，只截取前 2000 字节用于错误展示，避免淹没界面。
            QString detail = QString::fromUtf8(payload.left(2000));
            if (detail.trimmed().isEmpty()) {
                // 没有 HTTP 响应体时退回 Qt 的网络错误说明，例如连接超时或 DNS 失败。
                detail = networkErrorText;
            }
            emit requestFailed(QString("DeepSeek 请求失败（HTTP %1，Qt错误 %2）：%3")
                                   .arg(status)
                                   .arg(static_cast<int>(networkError))
                                   .arg(detail));
            return; // 失败响应绝不能继续当作正常模型消息交给 AgentController。
        }
        // 成功状态也不能盲信响应体，只有合法 JSON object 才交给 AgentController。
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit requestFailed("DeepSeek 返回了无法解析的 JSON"); // 统一走失败信号。
            return; // 后续代码需要 JSON object，因此结构非法时立即终止。
        }
        // 网络层只发出原始响应对象，choices/tool_calls 的业务解释由 Agent 层负责。
        emit responseReceived(document.object());
    });
}

} // namespace forge::assistant
