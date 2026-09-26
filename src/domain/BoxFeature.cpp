#include "domain/BoxFeature.h" // BoxFeature 类声明和 Feature/Parameter 基础类型。

#include "geometry/ShapeFactory.h" // 把合法尺寸转换为真正的 OCCT 长方体。

#include "domain/PositionParameters.h"

#include <stdexcept> // 未知参数名使用 invalid_argument 明确拒绝。
#include <utility>   // std::move 转移 ID 和 ParameterValue 内部资源。

namespace forge::domain {

BoxFeature::BoxFeature(
    std::string id,
    double length,
    double width,
    double height, double x, double y, double z)
    : Feature(std::move(id), "Box") // 公共基类保存实例 ID 和固定类型名 Box。
    , parameters_{                   // 参数顺序与 Registry 描述、rebuild 下标完全一致。
          {"length", length},
          {"width", width},
          {"height", height}, {"x", x}, {"y", y}, {"z", z}}
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
    return validatePrimitiveParameters(parameters_);
}

TopoDS_Shape BoxFeature::rebuild(const std::vector<TopoDS_Shape>&) const
{
    // Feature 只组织参数，真正调用 OCCT 的细节集中在 geometry::ShapeFactory。
    if (!validate().empty()) return {};
    // 先在局部坐标系构造，再应用世界坐标平移；布尔运算收到的已经是定位后的形状。
    return geometry::ShapeFactory::translate(
        geometry::ShapeFactory::makeBox(parameters_[0].asDouble(), parameters_[1].asDouble(), parameters_[2].asDouble()),
        parameters_[3].asDouble(), parameters_[4].asDouble(), parameters_[5].asDouble());
}

} // namespace forge::domain
