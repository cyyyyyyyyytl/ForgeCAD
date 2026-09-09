#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace forge::application {
class ModelDocument;
class ModelingService;
}

namespace forge::domain { class Feature; }

namespace forge::assistant {

// ============================================================
// ToolRegistry：LLM 与 C++ 业务代码之间的安全边界
// ------------------------------------------------------------
// 模型只能返回“工具名 + JSON 参数”，不能执行 C++，也接触不到 OCCT 指针。
// Registry 负责白名单分发、类型检查和错误封装，真正的修改再交给
// ModelingService。无论模型输出什么，未知工具和非法参数都不会越过这里。
// ============================================================
class ToolRegistry {
public:
    ToolRegistry(application::ModelDocument& document,
                 application::ModelingService& modelingService);

    QJsonArray schemas() const;
    QJsonObject execute(const QString& toolName, const QJsonObject& arguments);

private:
    QJsonObject featureToJson(const domain::Feature& feature) const;
    QJsonObject listFeatures() const;
    QJsonObject getFeature(const QJsonObject& arguments) const;
    QJsonObject createFeature(const QJsonObject& arguments);
    QJsonObject setParameter(const QJsonObject& arguments);

    application::ModelDocument& document_;
    application::ModelingService& modelingService_;
};

} // namespace forge::assistant
