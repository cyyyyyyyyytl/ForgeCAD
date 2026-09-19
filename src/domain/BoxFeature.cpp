#include "domain/BoxFeature.h" // BoxFeature 类声明和 Feature/Parameter 基础类型。

#include "geometry/ShapeFactory.h" // 把合法尺寸转换为真正的 OCCT 长方体。

#include <stdexcept> // 未知参数名使用 invalid_argument 明确拒绝。
#include <utility>   // std::move 转移 ID 和 ParameterValue 内部资源。

namespace forge::domain {

BoxFeature::BoxFeature(
    std::string id,
    double length,
    double width,
    double height)
    : Feature(std::move(id), "Box") // 公共基类保存实例 ID 和固定类型名 Box。
    , parameters_{                   // 参数顺序与 Registry 描述、rebuild 下标完全一致。
          {"length", length},
          {"width", width},
          {"height", height}}
{
    // 参数合法性由调用方通过 validate 检查；构造本身只建立对象状态。
}

// 返回 const 引用：调用方能读取参数，但不能绕过 setParameter 直接改 vector。
const std::vector<Parameter>& BoxFeature::parameters() const
{
    return parameters_;
}

void BoxFeature::setParameter(const std::string& name, ParameterValue value)
{
    // 参数数量很少，直接按名字遍历最清楚；不存在时明确拒绝拼错的名字。
    for (auto& parameter : parameters_) {
        if (parameter.name() == name) {
            // 找到同名项后替换值；move 支持未来字符串等较大 variant 成员。
            parameter.setValue(std::move(value));
            return; // 一个 Box 不会有重名参数，修改完成即可结束遍历。
        }
    }
    // 遍历完成仍未返回说明调用者拼错了参数名或使用了其他类型的参数。
    throw std::invalid_argument("Box 没有参数: " + name);
}

std::string BoxFeature::validate() const
{
    // 这里保护 Box 自身永远不能拥有非正尺寸，与 UI/AI 来源无关。
    for (const auto& parameter : parameters_) {
        if (parameter.asDouble() <= 0.0) {
            // 返回第一个发现的错误即可；调用方会把文字显示给用户或模型。
            return "参数 " + parameter.name() + " 必须大于 0";
        }
    }
    return {}; // 空 std::string 是项目约定的“全部校验通过”。
}

TopoDS_Shape BoxFeature::rebuild() const
{
    // Feature 只组织参数，真正调用 OCCT 的细节集中在 geometry::ShapeFactory。
    return geometry::ShapeFactory::makeBox(
        parameters_[0].asDouble(),
        parameters_[1].asDouble(),
        parameters_[2].asDouble());
}

} // namespace forge::domain
