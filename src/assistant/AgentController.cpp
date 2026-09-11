#include "assistant/AgentController.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace forge::assistant {

// ============================================================
// 构造：装配模型客户端、工具注册表和系统消息
// ------------------------------------------------------------
// document/modelingService 由 MainWindow 拥有，Controller 只保存到 ToolRegistry
// 的引用；client_ 是值成员，不需要手动 delete。
// ============================================================
AgentController::AgentController(application::ModelDocument& document,
                                 application::ModelingService& modelingService,
    QObject* parent)
    : QObject(parent)
    , client_()
    , tools_(document, modelingService)
{
    // 系统消息约束模型角色和安全行为：只有工具成功后才能声称已经修改模型。
    messages_.append(QJsonObject{
        {"role", "system"},
        {"content",
         "你是 ForgeCAD AI 建模助手。尺寸单位为毫米。"
         "只能使用提供的工具查询或修改模型；工具返回 success=true 后才能声称操作成功。"
         "缺少必要尺寸时先询问用户，不要猜测。回答使用简洁中文。"},
    });

    // 网络成功和失败分别进入两个处理槽，保持状态收口在 Controller。
    connect(&client_, &DeepSeekClient::responseReceived,
            this, &AgentController::handleResponse);
    connect(&client_, &DeepSeekClient::requestFailed,
            this, &AgentController::handleError);
}

// 接收 UI 输入并启动新一轮 Agent 循环。
void AgentController::submit(const QString& userMessage)
{
    // trim 后为空的输入没有语义，直接忽略且不污染对话历史。
    const QString text = userMessage.trimmed();
    if (text.isEmpty()) return;

    // MVP 同一时间只允许一个请求链，避免两轮 tool_call 历史交叉。
    if (busy_) {
        emit errorMessage("上一条请求仍在处理中");
        return;
    }

    // 对话历史由客户端保存，DeepSeek 每轮都会收到完整 messages。
    messages_.append(QJsonObject{{"role", "user"}, {"content", text}});
    // 每条用户消息重新计算最多 6 步的额度，历史消息本身继续保留。
    currentStep_ = 0;
    setBusy(true);
    requestNextTurn();
}

// 发送当前完整对话历史和最新工具 Schema；成功/失败通过信号异步回来。
void AgentController::requestNextTurn()
{
    // 状态栏显示当前步骤，用户可以看出是否发生了多轮工具调用。
    emit statusMessage(QString("正在请求 DeepSeek（步骤 %1/%2）…")
                           .arg(currentStep_ + 1)
                           .arg(MaxAgentSteps));
    client_.send(messages_, tools_.schemas());
}

// ============================================================
// handleResponse：解释一次模型响应，并决定结束还是继续循环
// ------------------------------------------------------------
// 分支 A：没有 tool_calls -> 展示最终文本、结束 busy 状态。
// 分支 B：存在 tool_calls -> 本地执行、追加 tool 结果、再次请求模型。
// ============================================================
void AgentController::handleResponse(const QJsonObject& response)
{
    // Chat Completions 用 choices 数组承载候选结果；MVP 只使用第一个候选。
    const QJsonArray choices = response.value("choices").toArray();
    if (choices.isEmpty() || !choices.first().isObject()) {
        handleError("DeepSeek 响应缺少 choices");
        return;
    }

    // choices[0].message 才包含 assistant 文本或 tool_calls。
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
        // 空 content 仍给用户一个可见结束提示，防止界面像“卡住”一样无反馈。
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

    // 一次响应可以请求多个工具，例如先创建 Box 再创建 Sphere，逐个执行并回传。
    for (const QJsonValue& rawCall : toolCalls) {
        // API 外层 call 提供配对 ID，function 内层提供工具名和 JSON 字符串参数。
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

        // 不论成功失败都告诉用户执行到了哪个工具；详细结果继续交给模型解释。
        emit statusMessage(QString("已执行工具：%1").arg(toolName));

        // 只有 Registry 明确标记 model_changed 才通知 UI 重建几何；查询工具不刷新。
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

    // 一批 tool_calls 算作一步。达到上限时熔断，不再向服务端继续递归请求。
    ++currentStep_;
    if (currentStep_ >= MaxAgentSteps) {
        handleError("Agent 达到最大工具调用步数，已停止");
        return;
    }
    requestNextTurn();
}

// 所有网络/协议错误统一结束 busy 状态并通过两个信号分别更新对话框和状态栏。
void AgentController::handleError(const QString& error)
{
    emit errorMessage(error);
    emit statusMessage("请求失败");
    setBusy(false);
}

// 集中维护 busy_，只有状态真实变化才发信号，避免 UI 重复启停输入控件。
void AgentController::setBusy(bool busy)
{
    if (busy_ == busy) return;
    busy_ = busy;
    emit busyChanged(busy_);
}

} // namespace forge::assistant
