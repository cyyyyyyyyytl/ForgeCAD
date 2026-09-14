#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace forge::application {
class ModelDocument;
}

namespace forge::domain { class Feature; }

namespace forge::assistant {

// ============================================================
// ToolRegistry：LLM 与 C++ 业务代码之间的安全边界
// ------------------------------------------------------------
// 模型只能返回“工具名 + JSON 参数”，不能执行 C++，也接触不到 OCCT 指针。
// Registry 负责白名单分发、类型检查和错误封装，真正的修改交给
// ModelDocument。无论模型输出什么，未知工具和非法参数都不会越过这里。
// 它同时承担两种格式之间的翻译：QJsonObject <-> 普通 C++ 模型数据。
// ============================================================
class ToolRegistry {
public:
    explicit ToolRegistry(application::ModelDocument& document);

    QJsonArray schemas() const; // 返回发给模型的全部 function tool JSON Schema。
    QJsonObject execute(const QString& toolName,
                        const QJsonObject& arguments); // 统一白名单分发并封装错误。

private:
    // 领域对象序列化辅助函数：只暴露 ID、类型和参数，不暴露 OCCT 句柄。
    QJsonObject featureToJson(const domain::Feature& feature) const;
    QJsonObject listFeatures() const;                         // list_features 实现。
    QJsonObject getFeature(const QJsonObject& arguments) const; // get_feature 实现。
    QJsonObject createFeature(const QJsonObject& arguments);  // create_feature 实现。
    QJsonObject setParameter(const QJsonObject& arguments);   // set_parameter 实现。

    // 非拥有引用：实际文档由 MainWindow 持有，并且比 ToolRegistry 活得久。
    application::ModelDocument& document_;
};

} // namespace forge::assistant
