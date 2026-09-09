#include "assistant/ToolRegistry.h"

#include "application/ModelDocument.h"
#include "application/ModelingService.h"
#include "domain/Feature.h"
#include "domain/FeatureCatalog.h"

#include <stdexcept>

namespace forge::assistant {
namespace {

QJsonObject functionTool(const QString& name,
                         const QString& description,
                         const QJsonObject& parameters)
{
    return {
        {"type", "function"},
        {"function", QJsonObject{
            {"name", name},
            {"description", description},
            {"parameters", parameters},
        }},
    };
}

QJsonObject objectSchema(const QJsonObject& properties,
                         const QJsonArray& required = {})
{
    return {
        {"type", "object"},
        {"properties", properties},
        {"required", required},
        {"additionalProperties", false},
    };
}

QString requireString(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        throw std::invalid_argument(("缺少字符串参数: " + key).toStdString());
    }
    return value.toString();
}

} // namespace

ToolRegistry::ToolRegistry(application::ModelDocument& document,
                           application::ModelingService& modelingService)
    : document_(document)
    , modelingService_(modelingService)
{
}

QJsonArray ToolRegistry::schemas() const
{
    // Feature 类型枚举从 Catalog 自动生成；以后新增 Cone 时不必手改 AI 类型列表。
    QJsonArray featureTypes;
    for (const auto& descriptor : domain::FeatureCatalog::all()) {
        featureTypes.append(QString::fromStdString(descriptor.type));
    }

    const QJsonObject featureIdProperty{
        {"type", "string"},
        {"description", "Feature ID，例如 Box001"},
    };

    return {
        functionTool(
            "list_features",
            "列出当前 ForgeCAD 文档中的所有特征及参数。",
            objectSchema({})),
        functionTool(
            "get_feature",
            "根据稳定 ID 查询一个特征。",
            objectSchema({{"feature_id", featureIdProperty}}, {"feature_id"})),
        functionTool(
            "create_feature",
            "创建 Box、Cylinder 或 Sphere。尺寸单位为毫米；parameters 必须使用特征登记的参数名。",
            objectSchema({
                {"type", QJsonObject{
                    {"type", "string"},
                    {"enum", featureTypes},
                }},
                {"parameters", QJsonObject{
                    {"type", "object"},
                    {"description", "具名数值参数，例如 Box 使用 length、width、height。"},
                    {"additionalProperties", QJsonObject{{"type", "number"}}},
                }},
            }, {"type", "parameters"})),
        functionTool(
            "set_parameter",
            "修改已有特征的一个数值参数。",
            objectSchema({
                {"feature_id", featureIdProperty},
                {"parameter_name", QJsonObject{{"type", "string"}}},
                {"value", QJsonObject{{"type", "number"}}},
            }, {"feature_id", "parameter_name", "value"})),
    };
}

QJsonObject ToolRegistry::execute(const QString& toolName, const QJsonObject& arguments)
{
    // 第一版只有四个白名单工具。这里故意不提供“执行任意代码/任意 OCCT API”。
    try {
        if (toolName == "list_features") return listFeatures();
        if (toolName == "get_feature") return getFeature(arguments);
        if (toolName == "create_feature") return createFeature(arguments);
        if (toolName == "set_parameter") return setParameter(arguments);
        throw std::invalid_argument(("未知工具: " + toolName).toStdString());
    } catch (const std::exception& error) {
        return {
            {"success", false},
            {"error", QString::fromUtf8(error.what())},
        };
    }
}

QJsonObject ToolRegistry::featureToJson(const domain::Feature& feature) const
{
    QJsonObject parameters;
    for (const auto& parameter : feature.parameters()) {
        parameters.insert(QString::fromStdString(parameter.name()), parameter.asDouble());
    }
    return {
        {"feature_id", QString::fromStdString(feature.id())},
        {"type", QString::fromStdString(feature.name())},
        {"parameters", parameters},
    };
}

QJsonObject ToolRegistry::listFeatures() const
{
    QJsonArray features;
    for (const auto& feature : document_.features()) {
        features.append(featureToJson(*feature));
    }
    return {
        {"success", true},
        {"features", features},
    };
}

QJsonObject ToolRegistry::getFeature(const QJsonObject& arguments) const
{
    const QString id = requireString(arguments, "feature_id");
    const domain::Feature* feature = document_.findFeature(id.toStdString());
    if (!feature) {
        throw std::invalid_argument(("找不到 Feature: " + id).toStdString());
    }
    QJsonObject result = featureToJson(*feature);
    result.insert("success", true);
    return result;
}

QJsonObject ToolRegistry::createFeature(const QJsonObject& arguments)
{
    const QString type = requireString(arguments, "type");
    const QJsonValue rawParameters = arguments.value("parameters");
    if (!rawParameters.isObject()) {
        throw std::invalid_argument("parameters 必须是 JSON 对象");
    }

    // QJsonObject -> 与 Qt 无关的领域参数 Map；从这里开始进入普通 C++ 应用层。
    domain::NumericParameters parameters;
    const QJsonObject parameterObject = rawParameters.toObject();
    for (auto it = parameterObject.begin(); it != parameterObject.end(); ++it) {
        if (!it.value().isDouble()) {
            throw std::invalid_argument(("参数必须是数值: " + it.key()).toStdString());
        }
        parameters.emplace(it.key().toStdString(), it.value().toDouble());
    }

    // Registry 不直接调用 FeatureFactory，确保 UI 和 AI 都服从同一建模用例规则。
    domain::Feature& feature = modelingService_.createFeature(type.toStdString(), parameters);
    QJsonObject result = featureToJson(feature);
    result.insert("success", true);
    result.insert("model_changed", true);
    return result;
}

QJsonObject ToolRegistry::setParameter(const QJsonObject& arguments)
{
    const QString featureId = requireString(arguments, "feature_id");
    const QString parameterName = requireString(arguments, "parameter_name");
    const QJsonValue rawValue = arguments.value("value");
    if (!rawValue.isDouble()) {
        throw std::invalid_argument("value 必须是数值");
    }

    modelingService_.setParameter(featureId.toStdString(),
                                  parameterName.toStdString(),
                                  rawValue.toDouble());
    const domain::Feature* feature = document_.findFeature(featureId.toStdString());
    QJsonObject result = featureToJson(*feature);
    result.insert("success", true);
    result.insert("model_changed", true);
    return result;
}

} // namespace forge::assistant
