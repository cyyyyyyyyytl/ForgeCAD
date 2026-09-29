#pragma once

#include "assistant/DeepSeekClient.h"
#include "assistant/ToolRegistry.h"

#include <QJsonArray>
#include <QObject>
#include <QStringList>
#include <functional>

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
    // 可注入异步传输，离线测试使用同一套 submit/response 循环，不接触密钥和网络。
    using RequestSender = std::function<void(const QJsonArray&, const QJsonArray&, bool)>;
    // 注入 Document，让工具读写当前窗口对应的模型。
    AgentController(application::ModelDocument& document,
                    QObject* parent = nullptr, RequestSender sender = {});

    void submit(const QString& userMessage); // 接收一条用户自然语言并启动 Agent 循环。
    bool isBusy() const { return busy_; }    // UI 用它阻止用户重复提交并发请求。
    void setUiToolHandler(ToolRegistry::UiToolHandler handler) { tools_.setUiToolHandler(std::move(handler)); }
    // 网络响应与离线测试共用入口，删除确认和界面刷新不能被另一条路径绕开。
    QJsonObject executeTool(const QString& name, const QJsonObject& arguments);

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
    void finishWithSummary(const QString& reason);
    QString progressSummary() const;
    QString geometrySummary() const;
    void finishLocally();

    static constexpr int MaxAgentSteps = 24; // 组合建模需要多轮查询、创建与布尔。
    static constexpr int MaxToolCalls = 96; // 一轮可能包含多个调用，另设总调用预算。
    static constexpr int MaxConsecutiveFailures = 3;
    DeepSeekClient client_;                // 只负责异步 HTTP，不理解 CAD 工具。
    const application::ModelDocument& document_; // 只读预览删除范围；写入仍统一走 ToolRegistry。
    ToolRegistry tools_;                   // AI 能进入 ModelDocument 的唯一受控入口。
    QJsonArray messages_;                  // 完整对话历史：system/user/assistant/tool。
    int currentStep_ = 0;                  // 当前请求已经完成的工具轮数。
    bool busy_ = false;                    // true 表示请求链尚未结束。
    RequestSender requestSender_;
    int toolCallCount_ = 0;
    int modelChangeCount_ = 0;
    int consecutiveFailures_ = 0;
    QStringList changedFeatureIds_;
    bool summaryOnly_ = false;
    QString stopReason_;
    QJsonObject latestGeometryAnalysis_; // 本轮修改后、本机只读检查的最终结果。
};

} // namespace forge::assistant
