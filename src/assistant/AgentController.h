#pragma once

#include "assistant/DeepSeekClient.h"
#include "assistant/ToolRegistry.h"

#include <QJsonArray>
#include <QObject>

namespace forge::application {
class ModelDocument;
class ModelingService;
}

namespace forge::assistant {

// ============================================================
// AgentController：最小 C++ Agent 运行时
// ------------------------------------------------------------
// Agent 并不是模型在本机运行函数，而是宿主程序维护下面的循环：
//   messages + tools -> 请求模型 -> 收到 tool_calls -> 本地执行
//   -> 追加 role=tool 的结果 -> 再请求模型 -> 收到最终文本
// MaxAgentSteps 是熔断器，避免模型错误地无限调用工具。
// ============================================================
class AgentController : public QObject {
    Q_OBJECT

public:
    AgentController(application::ModelDocument& document,
                    application::ModelingService& modelingService,
                    QObject* parent = nullptr);

    void submit(const QString& userMessage);
    bool isBusy() const { return busy_; }

signals:
    void assistantMessage(const QString& message);
    void statusMessage(const QString& message);
    void errorMessage(const QString& message);
    void modelChanged(const QString& featureId);
    void busyChanged(bool busy);

private slots:
    void handleResponse(const QJsonObject& response);
    void handleError(const QString& error);

private:
    void requestNextTurn();
    void setBusy(bool busy);

    static constexpr int MaxAgentSteps = 6;
    DeepSeekClient client_;
    ToolRegistry tools_;
    QJsonArray messages_;
    int currentStep_ = 0;
    bool busy_ = false;
};

} // namespace forge::assistant
