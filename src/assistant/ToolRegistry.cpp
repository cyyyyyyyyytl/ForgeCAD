#include "assistant/ToolRegistry.h"

#include "application/ModelDocument.h"
#include "application/ModelingService.h"
#include "domain/Feature.h"
#include "domain/FeatureCatalog.h"

#include <stdexcept>

namespace forge::assistant {
namespace {

// 把一个普通 C++ 工具描述包装成大模型 API 约定的 function tool 结构：
// {"type":"function", "function":{"name":..., "parameters":...}}。
// 单独抽成辅助函数，避免 schemas() 中四次重复外层 JSON 格式。
QJsonObject functionTool(const QString& name,
                         const QString& description,
                         const QJsonObject& parameters)
{
    // QJsonObject 的初始化列表会递归生成最终 HTTP 请求中的 JSON 对象。
    return {
        {"type", "function"},
        {"function", QJsonObject{
            {"name", name},
            {"description", description},
            {"parameters", parameters},
        }},
    };
}

// 创建通用 JSON Schema object。additionalProperties=false 表示顶层不接受
// Schema 未声明字段，可减少模型拼错参数名时被静默忽略的风险。
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

// 从工具参数中读取必填字符串。所有工具共用这条规则：字段必须存在、
// 类型必须是 string、去掉空白后不能是空串；否则统一抛给 execute() 封装。
QString requireString(const QJsonObject& object, const QString& key)
{
    // QJsonObject::value 在键不存在时返回 Undefined，isString() 会自然失败。
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
    // Registry 不拥有这两个对象；MainWindow 保证 Document 和 Service
    // 生命周期覆盖 AgentController 及其 ToolRegistry。
}

// ============================================================
// schemas：把 ForgeCAD 当前允许的能力公开给大模型
// ------------------------------------------------------------
// Schema 只是告诉模型“可以怎样申请调用”；它不是安全校验的替代品。
// 模型返回后仍会经过 requireString、数值类型检查和 ModelingService 校验。
// ============================================================
QJsonArray ToolRegistry::schemas() const
{
    // Feature 类型枚举从 Catalog 自动生成；以后新增 Cone 时不必手改 AI 类型列表。
    QJsonArray featureTypes;
    for (const auto& descriptor : domain::FeatureCatalog::all()) {
        featureTypes.append(QString::fromStdString(descriptor.type));
    }

    // 多个工具共用相同的 feature_id 字段描述，抽出来避免文字不一致。
    const QJsonObject featureIdProperty{
        {"type", "string"},
        {"description", "Feature ID，例如 Box001"},
    };

    // list/get 是只读工具；create/set 会修改 Document 并返回 model_changed=true。
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

// ============================================================
// execute：工具调用总入口
// ------------------------------------------------------------
// AgentController 无需知道具体工具实现，只把 name 和 arguments 交给这里。
// 所有异常都转换为 {success:false,error:...} 返回给模型，让模型可以解释、
// 修正参数或询问用户，而不是让一次错误导致桌面程序崩溃。
// ============================================================
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
        // QString::fromUtf8 保留 C++ 异常消息中的中文内容。
        return {
            {"success", false},
            {"error", QString::fromUtf8(error.what())},
        };
    }
}

// 把领域对象转换成模型容易理解的 JSON 快照。
// 这里只暴露稳定 ID、类型和数值参数，不把 TopoDS_Shape 或内存地址交给模型。
QJsonObject ToolRegistry::featureToJson(const domain::Feature& feature) const
{
    // Parameter 列表转为具名对象，例如 {"length":100,"width":50}。
    QJsonObject parameters;
    for (const auto& parameter : feature.parameters()) {
        parameters.insert(QString::fromStdString(parameter.name()), parameter.asDouble());
    }
    // 统一返回结构能让 list/get/create/set 的结果保持一致。
    return {
        {"feature_id", QString::fromStdString(feature.id())},
        {"type", QString::fromStdString(feature.name())},
        {"parameters", parameters},
    };
}

// 只读工具：遍历 Document 当前顺序，把每个 Feature 序列化后放进数组。
QJsonObject ToolRegistry::listFeatures() const
{
    // 空文档合法，返回 success=true 和空数组，而不是把“没有模型”当成错误。
    QJsonArray features;
    for (const auto& feature : document_.features()) {
        features.append(featureToJson(*feature));
    }
    return {
        {"success", true},
        {"features", features},
    };
}

// 只读工具：按稳定 ID 查询单个 Feature。
QJsonObject ToolRegistry::getFeature(const QJsonObject& arguments) const
{
    // 先验证协议参数，再进入 Document；模型缺字段时不会触发空 ID 查询。
    const QString id = requireString(arguments, "feature_id");
    const domain::Feature* feature = document_.findFeature(id.toStdString());
    if (!feature) {
        throw std::invalid_argument(("找不到 Feature: " + id).toStdString());
    }
    // 在统一 Feature JSON 上增加执行状态，供模型可靠判断查询是否成功。
    QJsonObject result = featureToJson(*feature);
    result.insert("success", true);
    return result;
}

// 修改工具：根据类型和具名参数创建新 Feature。
QJsonObject ToolRegistry::createFeature(const QJsonObject& arguments)
{
    // type 决定 FeatureCatalog 中使用哪套参数 Schema。
    const QString type = requireString(arguments, "type");

    // parameters 必须是对象，数组或纯文本无法表达“参数名 -> 数值”的映射。
    const QJsonValue rawParameters = arguments.value("parameters");
    if (!rawParameters.isObject()) {
        throw std::invalid_argument("parameters 必须是 JSON 对象");
    }

    // QJsonObject -> 与 Qt 无关的领域参数 Map；从这里开始进入普通 C++ 应用层。
    domain::NumericParameters parameters;
    const QJsonObject parameterObject = rawParameters.toObject();
    for (auto it = parameterObject.begin(); it != parameterObject.end(); ++it) {
        // QJsonValue::isDouble 对 JSON 的整数和小数都成立，统一转成 double。
        if (!it.value().isDouble()) {
            throw std::invalid_argument(("参数必须是数值: " + it.key()).toStdString());
        }
        parameters.emplace(it.key().toStdString(), it.value().toDouble());
    }

    // Registry 不直接调用 FeatureFactory，确保 UI 和 AI 都服从同一建模用例规则。
    domain::Feature& feature = modelingService_.createFeature(type.toStdString(), parameters);
    // 返回完整新对象而不只返回“成功”，方便模型准确告诉用户生成了什么。
    QJsonObject result = featureToJson(feature);
    result.insert("success", true);
    result.insert("model_changed", true);
    return result;
}

// 修改工具：按 Feature ID 和参数名更新一个数值。
QJsonObject ToolRegistry::setParameter(const QJsonObject& arguments)
{
    // 三个字段分别解决“改谁、改什么、改成多少”。
    const QString featureId = requireString(arguments, "feature_id");
    const QString parameterName = requireString(arguments, "parameter_name");
    const QJsonValue rawValue = arguments.value("value");
    if (!rawValue.isDouble()) {
        throw std::invalid_argument("value 必须是数值");
    }

    // 真正的范围检查、旧值保存和失败回滚全部由 ModelingService 负责。
    modelingService_.setParameter(featureId.toStdString(),
                                  parameterName.toStdString(),
                                  rawValue.toDouble());
    // 修改成功后重新从 Document 读取，返回的是最终生效值而不是模型请求值。
    const domain::Feature* feature = document_.findFeature(featureId.toStdString());
    QJsonObject result = featureToJson(*feature);
    result.insert("success", true);
    result.insert("model_changed", true);
    return result;
}

} // namespace forge::assistant
