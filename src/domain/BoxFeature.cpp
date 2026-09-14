#include "domain/BoxFeature.h"

#include "geometry/ShapeFactory.h"

#include <stdexcept>
#include <utility>

namespace forge::domain {

BoxFeature::BoxFeature(
    std::string id,
    double length,
    double width,
    double height)
    : Feature(std::move(id), "Box")
    , parameters_{{"length", length}, {"width", width}, {"height", height}}
{
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
            parameter.setValue(std::move(value));
            return;
        }
    }
    throw std::invalid_argument("Box 没有参数: " + name);
}

std::string BoxFeature::validate() const
{
    // 这里保护 Box 自身永远不能拥有非正尺寸，与 UI/AI 来源无关。
    for (const auto& parameter : parameters_) {
        if (parameter.asDouble() <= 0.0) {
            return "参数 " + parameter.name() + " 必须大于 0";
        }
    }
    return {};
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
