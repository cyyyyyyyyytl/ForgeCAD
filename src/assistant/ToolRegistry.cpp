#include "assistant/ToolRegistry.h" // ToolRegistry 的公开接口声明。

#include "application/ModelDocument.h" // 真正执行查询和修改的文档 API。
#include "domain/Feature.h"            // 读取 Feature 的 ID、类型和参数。
#include "domain/FeatureRegistry.h"    // 生成可用类型枚举并创建具体 Feature。

#include <stdexcept> // invalid_argument 用于统一表达模型参数或工具名错误。

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

ToolRegistry::ToolRegistry(application::ModelDocument& document)
    : document_(document) // 保存引用，不复制整份文档，也不接管其生命周期。
{
    // Registry 不拥有 Document；MainWindow 保证它的生命周期更长。
}

// ============================================================
// schemas：把 ForgeCAD 当前允许的能力公开给大模型
// ------------------------------------------------------------
// Schema 只是告诉模型“可以怎样申请调用”；它不是安全校验的替代品。
// 模型返回后仍会经过 requireString、数值类型检查和 Document 校验。
// ============================================================
QJsonArray ToolRegistry::schemas() const
{
    // Feature 类型枚举从 Registry 自动生成；以后新增 Cone 时不必手改 AI 类型列表。
    QJsonArray featureTypes;
    for (const auto& descriptor : domain::FeatureRegistry::all()) {
        featureTypes.append(QString::fromStdString(descriptor.type));
    }

    // 多个工具共用相同的 feature_id 字段描述，抽出来避免文字不一致。
    const QJsonObject featureIdProperty{
        {"type", "string"},
        {"description", "Feature ID，例如 Box001"},
    };

    // list/get 是只读工具；create/set/delete 会修改 Document 并返回 model_changed=true。
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
            "创建 Box、Cylinder 或 Sphere。尺寸和世界坐标位置单位为毫米；可选 x/y/z 默认 0，允许负值。Box 位置为基准角点，Cylinder 为底面圆心，Sphere 为球心；parameters 必须使用特征登记的参数名。",
            objectSchema({
                {"type", QJsonObject{
                    {"type", "string"},
                    {"enum", featureTypes},
                }},
                {"parameters", QJsonObject{
                    {"type", "object"},
                    {"description", "具名数值参数：Box 必填 length/width/height，Cylinder 必填 radius/height，Sphere 必填 radius；各类型可选 x/y/z，默认 0。"},
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
        functionTool(
            "delete_feature",
            "按 Feature ID 删除已有特征；ForgeCAD 会先弹出本机确认框。",
            objectSchema({{"feature_id", featureIdProperty}}, {"feature_id"})),
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
    // 只允许明确登记的五个工具，不提供“执行任意代码/任意 OCCT API”。
    try {
        if (toolName == "list_features") return listFeatures();       // 无参数，只读全部对象。
        if (toolName == "get_feature") return getFeature(arguments); // 按稳定 ID 只读一个对象。
        if (toolName == "create_feature") return createFeature(arguments); // 创建并记录 Undo。
        if (toolName == "set_parameter") return setParameter(arguments);   // 修改并记录 Undo。
        if (toolName == "delete_feature") return deleteFeature(arguments); // 删除并记录 Undo。
        // 任何未登记名称都拒绝，模型无法借此调用任意本地函数。
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
QJsonObject ToolRegistry::featureToJson(const domain::Feature& feature, const core::ShapeResult* knownResult) const
{
    // Parameter 列表转为具名对象，例如 {"length":100,"width":50}。
    QJsonObject parameters;
    for (const auto& parameter : feature.parameters()) {
        // JSON 对象以参数名为键、当前数值为值，天然适合模型读取和再次引用。
        parameters.insert(QString::fromStdString(parameter.name()), parameter.asDouble());
    }
    const core::ShapeResult result = knownResult ? *knownResult : document_.rebuildReport().at(feature.id());
    const char* state = "failed";
    switch (result.status) {
    case core::RebuildStatus::Ready: state = "ready"; break;
    case core::RebuildStatus::Empty: state = "empty"; break;
    case core::RebuildStatus::Failed: state = "failed"; break;
    case core::RebuildStatus::Blocked: state = "blocked"; break;
    }
    // 统一返回结构能让 list/get/create/set 的结果保持一致。
    return {
        {"feature_id", QString::fromStdString(feature.id())},
        {"type", QString::fromStdString(feature.type())},
        {"parameters", parameters},
        {"rebuild_status", state},
        {"rebuild_message", QString::fromStdString(result.message)},
    };
}

// 只读工具：遍历 Document 当前顺序，把每个 Feature 序列化后放进数组。
QJsonObject ToolRegistry::listFeatures() const
{
    // 空文档合法，返回 success=true 和空数组，而不是把“没有模型”当成错误。
    QJsonArray features;
    const auto report = document_.rebuildReport();
    for (const auto& feature : document_.features()) {
        // unique_ptr 解引用后得到真正的 Feature，再转换为不含指针的 JSON 快照。
        features.append(featureToJson(*feature, &report.at(feature->id())));
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
    result.insert("success", true); // 成功字段让模型不必通过缺少 error 来猜测结果。
    return result;
}

// 修改工具：根据类型和具名参数创建新 Feature。
QJsonObject ToolRegistry::createFeature(const QJsonObject& arguments)
{
    // type 决定 FeatureRegistry 中使用哪套参数说明。
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
        // emplace 将 Qt 字符串/数值转换成领域层使用的 std::string/double。
        parameters.emplace(it.key().toStdString(), it.value().toDouble());
    }

    // 通过 Document 创建，确保 UI 和 AI 共用同一套校验和历史记录。
    domain::Feature& feature = document_.createFeature(type.toStdString(), parameters);
    // 返回完整新对象而不只返回“成功”，方便模型准确告诉用户生成了什么。
    QJsonObject result = featureToJson(feature);
    result.insert("success", true);        // 告诉模型本地创建已经真实成功。
    result.insert("model_changed", true);  // 告诉 Controller 需要刷新树、属性和三维视图。
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

    // 真正的范围检查和历史记录全部由 Document 负责。
    document_.setParameter(featureId.toStdString(),
                           parameterName.toStdString(),
                           rawValue.toDouble());
    // 修改成功后重新从 Document 读取，返回的是最终生效值而不是模型请求值。
    const domain::Feature* feature = document_.findFeature(featureId.toStdString());
    QJsonObject result = featureToJson(*feature);
    result.insert("success", true);       // 工具执行结果可被下一轮模型可靠判断。
    result.insert("model_changed", true); // 参数变化会影响几何，因此要求 UI 重建。
    return result;
}

// 删除工具只负责业务写入；Agent 已在调用 execute() 前处理用户确认。
QJsonObject ToolRegistry::deleteFeature(const QJsonObject& arguments)
{
    // 缺少 ID 或 ID 不是非空字符串时，统一交给 execute() 捕获并返回错误 JSON。
    const QString id = requireString(arguments, "feature_id");
    // 先记录整组受影响 ID；删除后对象已不存在，不能再查询它们。
    QJsonArray deletedIds;
    for (const std::string& affectedId : document_.deletionOrder(id.toStdString())) {
        deletedIds.append(QString::fromStdString(affectedId));
    }
    // Document 负责确认目标存在、删除对象并保存可撤销的旧状态。
    document_.deleteFeature(id.toStdString());
    // 已删除对象不能再解引用；回传稳定 ID 供界面刷新并供模型总结。
    return {
        {"success", true},
        {"model_changed", true},
        {"feature_id", id},
        {"deleted_feature_ids", deletedIds},
    };
}

} // namespace forge::assistant
