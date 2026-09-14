#pragma once

#include "assistant/DeepSeekClient.h"
#include "assistant/ToolRegistry.h"

#include <QJsonArray>
#include <QObject>

namespace forge::application {
class ModelDocument;
}

namespace forge::assistant {

// ============================================================
// AgentController：最小 C++ Agent 运行时
// ------------------------------------------------------------
// AI 层分成三层：
//   DeepSeekClient  <-> HTTP/JSON 网络通信
//   AgentController <-> 对话历史与“模型-工具-模型”循环
//   ToolRegistry    <-> 工具白名单与 ModelDocument 调用
//
// Agent 并不是模型在本机运行函数，而是宿主程序维护下面的循环：
//   messages + tools -> 请求模型 -> 收到 tool_calls -> 本地执行
//   -> 追加 role=tool 的结果 -> 再请求模型 -> 收到最终文本
// MaxAgentSteps 是熔断器，避免模型错误地无限调用工具。
// ============================================================
class AgentController : public QObject {
    Q_OBJECT

public:
    // 注入 Document，让工具读写当前窗口对应的模型。
    AgentController(application::ModelDocument& document,
                    QObject* parent = nullptr);

    void submit(const QString& userMessage); // 接收一条用户自然语言并启动 Agent 循环。
    bool isBusy() const { return busy_; }    // UI 用它阻止用户重复提交并发请求。

signals:
    void assistantMessage(const QString& message); // 最终自然语言答案，追加到聊天记录。
    void statusMessage(const QString& message);    // 短状态，显示在 MainWindow 状态栏。
    void errorMessage(const QString& message);     // 网络/协议/循环错误，显示在聊天区。
    void modelChanged(const QString& featureId);   // 工具修改成功，通知 UI 选中并重建模型。
    void busyChanged(bool busy);                   // 通知 UI 启用或禁用输入框和发送按钮。

private slots:
    void handleResponse(const QJsonObject& response); // DeepSeek 请求成功后的协议分支处理。
    void handleError(const QString& error);           // 任意请求失败后的统一收尾。

private:
    void requestNextTurn();      // 携带最新 messages 和 tools 发起下一轮请求。
    void setBusy(bool busy);     // 集中更新 busy_ 并只在变化时发信号。

    static constexpr int MaxAgentSteps = 6; // 单次用户请求的最大工具轮数，防止死循环。
    DeepSeekClient client_;                // 只负责异步 HTTP，不理解 CAD 工具。
    ToolRegistry tools_;                   // AI 能进入 ModelDocument 的唯一受控入口。
    QJsonArray messages_;                  // 完整对话历史：system/user/assistant/tool。
    int currentStep_ = 0;                  // 当前请求已经完成的工具轮数。
    bool busy_ = false;                    // true 表示请求链尚未结束。
};

} // namespace forge::assistant
