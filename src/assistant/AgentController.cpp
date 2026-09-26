#include "assistant/AgentController.h" // AgentController 类及其两个成员组件的声明。
#include "application/ModelDocument.h" // 删除前读取包含所有下游特征的预览名单。

#include <QJsonDocument>   // 在 JSON 对象和网络传输用字节串之间转换。
#include <QJsonParseError> // 保存 JSON 解析失败的具体状态。
#include <QMessageBox>    // AI 请求删除时，等待用户在本机确认目标特征。
#include <QStringList>    // 把级联删除的特征 ID 排版为逐行清单。

namespace forge::assistant {

// ============================================================
// 构造：装配模型客户端、工具注册表和系统消息
// ------------------------------------------------------------
// document 由 MainWindow 拥有，Controller 只把它交给 ToolRegistry 引用。
// ============================================================
AgentController::AgentController(application::ModelDocument& document,
    QObject* parent)
    : QObject(parent)   // 把 Controller 加入 Qt 父子对象树，交给 parent 管理生命周期。
    , client_()        // 构造只负责网络通信的 DeepSeekClient 值成员。
    , document_(document) // 只读查看删除范围，不通过此引用直接修改文档。
    , tools_(document) // 把当前 CAD 文档以非拥有引用注入工具注册表。
{
    // 系统消息约束模型角色和安全行为：只有工具成功后才能声称已经修改模型。
    messages_.append(QJsonObject{
        {"role", "system"}, // system 消息优先级最高，用来定义助手身份和边界。
        {"content",
         "你是 ForgeCAD AI 建模助手。尺寸单位为毫米。"
         "只能使用提供的工具查询或修改模型；工具返回 success=true 后才能声称操作成功。"
         "工具 success 只表示数据操作成功；应检查 rebuild_status，failed/blocked 不能声称几何生成成功，empty 应解释为空结果。"
         "缺少必要尺寸时先询问用户，不要猜测。"
         "删除操作若被用户取消，不要重试，应告知用户未删除。回答使用简洁中文。"},
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
    const QString text = userMessage.trimmed(); // 生成去除首尾空白后的新字符串。
    if (text.isEmpty()) return;                 // 空字符串不进入历史，也不发网络请求。

    // MVP 同一时间只允许一个请求链，避免两轮 tool_call 历史交叉。
    if (busy_) {
        emit errorMessage("上一条请求仍在处理中"); // 通知聊天窗口显示可理解的原因。
        return;                                  // 保留正在运行的请求链，不启动第二条。
    }

    // 对话历史由客户端保存，DeepSeek 每轮都会收到完整 messages。
    messages_.append(QJsonObject{{"role", "user"}, {"content", text}});
    // 每条用户消息重新计算最多 6 步的额度，历史消息本身继续保留。
    currentStep_ = 0;   // 新用户消息从 Agent 第 0 步重新计数。
    setBusy(true);      // 锁住输入框，并向外发出 busyChanged(true)。
    requestNextTurn();  // 发起第一轮“消息 + 工具说明”请求。
}

// 发送当前完整对话历史和最新工具 Schema；成功/失败通过信号异步回来。
void AgentController::requestNextTurn()
{
    // 状态栏显示当前步骤，用户可以看出是否发生了多轮工具调用。
    emit statusMessage(QString("正在请求 DeepSeek（步骤 %1/%2）…")
                           .arg(currentStep_ + 1)
                           .arg(MaxAgentSteps));
    client_.send(messages_, tools_.schemas()); // 网络异步返回，当前函数不会阻塞界面。
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
        handleError("DeepSeek 响应缺少 choices"); // 统一显示错误并解除 busy 状态。
        return;                                  // 响应结构不可信，不能继续向下取值。
    }

    // choices[0].message 才包含 assistant 文本或 tool_calls。
    const QJsonObject message = choices.first().toObject().value("message").toObject();
    if (message.isEmpty()) {
        handleError("DeepSeek 响应缺少 message"); // choices 存在但缺少真正的回答对象。
        return;                                  // 立即停止本轮解析。
    }

    // toArray() 在字段不存在时得到空数组，因此同一判断同时覆盖“无字段”和“空字段”。
    const QJsonArray toolCalls = message.value("tool_calls").toArray();
    if (toolCalls.isEmpty()) {
        // 没有 tool_calls 表示模型已经给出最终自然语言答案，本轮 Agent 结束。
        const QString content = message.value("content").toString().trimmed();
        // 保存最终回答，使下一条用户消息仍拥有连续上下文。
        messages_.append(QJsonObject{{"role", "assistant"}, {"content", content}});
        // 空 content 仍给用户一个可见结束提示，防止界面像“卡住”一样无反馈。
        emit assistantMessage(content.isEmpty() ? "请求已完成。" : content);
        emit statusMessage("就绪"); // 主窗口状态栏恢复为普通待命状态。
        setBusy(false);            // 重新启用输入框和发送按钮。
        return;                    // 最终文本已产生，本次 Agent 循环结束。
    }

    // 必须把包含 tool_calls 的 assistant 消息原样加入历史；下一轮 tool 结果
    // 通过 tool_call_id 与它配对，否则模型 API 无法识别工具调用上下文。
    QJsonObject assistantHistory{
        {"role", "assistant"},
        {"content", message.value("content")},
        {"tool_calls", toolCalls},
    };
    messages_.append(assistantHistory); // 先保存调用请求，后面的 tool 结果才能按 ID 配对。

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
        QJsonParseError parseError; // fromJson 会把成功或失败状态写入这个输出参数。
        const QJsonDocument argumentDocument = QJsonDocument::fromJson(argumentBytes, &parseError);
        QJsonObject result; // 无论解析成功与否，都构造统一 JSON 结果回传给模型。
        if (parseError.error != QJsonParseError::NoError || !argumentDocument.isObject()) {
            result = {
                {"success", false},
                {"error", "工具 arguments 不是合法 JSON 对象"},
            };
        } else if (toolName == "delete_feature") {
            // 删除先检查目标是否存在，再弹确认框；确认前不调用有写入能力的工具。
            QJsonObject args = argumentDocument.object();
            const QJsonValue rawId = args.value("feature_id");
            const QString id = rawId.toString().trimmed();
            if (!rawId.isString() || id.isEmpty()) {
                result = {
                    {"success", false},
                    {"error", "缺少要删除的 feature_id"},
                };
            } else {
                // 把展示给用户的 ID 与真正提交给工具的 ID 统一成去空格后的值。
                args.insert("feature_id", id);
                const QJsonObject target = tools_.execute("get_feature", {{"feature_id", id}});
                if (!target.value("success").toBool()) {
                    result = target; // 目标不存在时沿用工具错误，不弹没有意义的确认框。
                } else {
                    const std::string featureId = id.toStdString();
                    const auto deletionIds = document_.deletionOrder(featureId);
                    QStringList dependentIds;
                    for (const std::string& affectedId : deletionIds) {
                        if (affectedId != featureId) {
                            dependentIds.append(QString::fromStdString(affectedId));
                        }
                    }

                    QString prompt = QString("确定删除 %1（%2）吗？")
                                         .arg(id, target.value("type").toString());
                    if (!dependentIds.isEmpty()) {
                        prompt += QString("\n\n以下依赖特征也会一起删除：\n%1")
                                      .arg(dependentIds.join("\n"));
                    }
                    const auto answer = QMessageBox::question(
                        qobject_cast<QWidget*>(parent()),
                        "确认删除",
                        prompt,
                        QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::No);
                    if (answer == QMessageBox::Yes) {
                        result = tools_.execute(toolName, args);
                    } else {
                        result = {
                            {"success", false},
                            {"error", "用户取消删除"},
                        };
                    }
                }
            }
        } else {
            // 其他工具保持统一分发，不需要在 Agent 中逐个识别工具名。
            result = tools_.execute(toolName, argumentDocument.object());
        }

        // 取消或参数错误也会生成工具结果，但不能在状态栏误称执行成功。
        emit statusMessage(QString("工具%1：%2")
                               .arg(result.value("success").toBool() ? "成功" : "未完成", toolName));

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
    ++currentStep_; // 一次响应中的多个并行 tool_calls 合计为一个 Agent 步骤。
    if (currentStep_ >= MaxAgentSteps) {
        handleError("Agent 达到最大工具调用步数，已停止"); // 熔断可能的模型循环。
        return;                                          // 不再发送下一次网络请求。
    }
    requestNextTurn(); // 把刚追加的 role=tool 结果发回模型，请它继续判断或总结。
}

// 所有网络/协议错误统一结束 busy 状态并通过两个信号分别更新对话框和状态栏。
void AgentController::handleError(const QString& error)
{
    emit errorMessage(error);       // 对话框显示详细错误内容。
    emit statusMessage("请求失败"); // 主窗口状态栏显示简短状态。
    setBusy(false);                 // 无论哪种错误都必须重新允许用户输入。
}

// 集中维护 busy_，只有状态真实变化才发信号，避免 UI 重复启停输入控件。
void AgentController::setBusy(bool busy)
{
    if (busy_ == busy) return; // 状态没有变化时不重复发信号，减少无意义的 UI 更新。
    busy_ = busy;              // 先更新内部真值，槽函数收到信号时查询结果才一致。
    emit busyChanged(busy_);   // 通知 AssistantDialog 启用或禁用交互控件。
}

} // namespace forge::assistant
