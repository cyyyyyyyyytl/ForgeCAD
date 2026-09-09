#include "assistant/AgentController.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace forge::assistant {

AgentController::AgentController(application::ModelDocument& document,
                                 application::ModelingService& modelingService,
    QObject* parent)
    : QObject(parent)
    , client_()
    , tools_(document, modelingService)
{
    messages_.append(QJsonObject{
        {"role", "system"},
        {"content",
         "你是 ForgeCAD AI 建模助手。尺寸单位为毫米。"
         "只能使用提供的工具查询或修改模型；工具返回 success=true 后才能声称操作成功。"
         "缺少必要尺寸时先询问用户，不要猜测。回答使用简洁中文。"},
    });

    connect(&client_, &DeepSeekClient::responseReceived,
            this, &AgentController::handleResponse);
    connect(&client_, &DeepSeekClient::requestFailed,
            this, &AgentController::handleError);
}

void AgentController::submit(const QString& userMessage)
{
    const QString text = userMessage.trimmed();
    if (text.isEmpty()) return;
    if (busy_) {
        emit errorMessage("上一条请求仍在处理中");
        return;
    }

    // 对话历史由客户端保存，DeepSeek 每轮都会收到完整 messages。
    messages_.append(QJsonObject{{"role", "user"}, {"content", text}});
    currentStep_ = 0;
    setBusy(true);
    requestNextTurn();
}

void AgentController::requestNextTurn()
{
    emit statusMessage(QString("正在请求 DeepSeek（步骤 %1/%2）…")
                           .arg(currentStep_ + 1)
                           .arg(MaxAgentSteps));
    client_.send(messages_, tools_.schemas());
}

void AgentController::handleResponse(const QJsonObject& response)
{
    const QJsonArray choices = response.value("choices").toArray();
    if (choices.isEmpty() || !choices.first().isObject()) {
        handleError("DeepSeek 响应缺少 choices");
        return;
    }

    const QJsonObject message = choices.first().toObject().value("message").toObject();
    if (message.isEmpty()) {
        handleError("DeepSeek 响应缺少 message");
        return;
    }

    const QJsonArray toolCalls = message.value("tool_calls").toArray();
    if (toolCalls.isEmpty()) {
        // 没有 tool_calls 表示模型已经给出最终自然语言答案，本轮 Agent 结束。
        const QString content = message.value("content").toString().trimmed();
        messages_.append(QJsonObject{{"role", "assistant"}, {"content", content}});
        emit assistantMessage(content.isEmpty() ? "请求已完成。" : content);
        emit statusMessage("就绪");
        setBusy(false);
        return;
    }

    // 必须把包含 tool_calls 的 assistant 消息原样加入历史；下一轮 tool 结果
    // 通过 tool_call_id 与它配对，否则模型 API 无法识别工具调用上下文。
    QJsonObject assistantHistory{
        {"role", "assistant"},
        {"content", message.value("content")},
        {"tool_calls", toolCalls},
    };
    messages_.append(assistantHistory);

    for (const QJsonValue& rawCall : toolCalls) {
        const QJsonObject call = rawCall.toObject();
        const QString callId = call.value("id").toString();
        const QJsonObject function = call.value("function").toObject();
        const QString toolName = function.value("name").toString();
        const QByteArray argumentBytes = function.value("arguments").toString().toUtf8();

        // function.arguments 在 API 响应里是“包含 JSON 的字符串”，仍可能格式错误，
        // 所以这里必须再次解析并验证，不能直接信任模型输出。
        QJsonParseError parseError;
        const QJsonDocument argumentDocument = QJsonDocument::fromJson(argumentBytes, &parseError);
        QJsonObject result;
        if (parseError.error != QJsonParseError::NoError || !argumentDocument.isObject()) {
            result = {
                {"success", false},
                {"error", "工具 arguments 不是合法 JSON 对象"},
            };
        } else {
            result = tools_.execute(toolName, argumentDocument.object());
        }

        emit statusMessage(QString("已执行工具：%1").arg(toolName));
        if (result.value("model_changed").toBool()) {
            emit modelChanged(result.value("feature_id").toString());
        }

        // 工具返回值不是给 UI 的最终回答，而是作为 role=tool 再喂给模型，
        // 让模型判断是否继续调用工具，或向用户总结执行结果。
        messages_.append(QJsonObject{
            {"role", "tool"},
            {"tool_call_id", callId},
            {"content", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))},
        });
    }

    ++currentStep_;
    if (currentStep_ >= MaxAgentSteps) {
        handleError("Agent 达到最大工具调用步数，已停止");
        return;
    }
    requestNextTurn();
}

void AgentController::handleError(const QString& error)
{
    emit errorMessage(error);
    emit statusMessage("请求失败");
    setBusy(false);
}

void AgentController::setBusy(bool busy)
{
    if (busy_ == busy) return;
    busy_ = busy;
    emit busyChanged(busy_);
}

} // namespace forge::assistant
