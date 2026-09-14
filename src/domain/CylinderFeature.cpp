#include "domain/CylinderFeature.h"

#include "geometry/ShapeFactory.h"

#include <stdexcept>
#include <utility>

namespace forge::domain {

CylinderFeature::CylinderFeature(std::string id, double radius, double height)
    : Feature(std::move(id), "Cylinder")
    , parameters_{{"radius", radius}, {"height", height}}
{
}

const std::vector<Parameter>& CylinderFeature::parameters() const
{
    // 返回当前圆柱参数的只读视图。
    return parameters_;
}

void CylinderFeature::setParameter(const std::string& name, ParameterValue value)
{
    // 只修改名字匹配的参数；范围由 Document 检查，未知名字由 Feature 拒绝。
    for (auto& parameter : parameters_) {
        if (parameter.name() == name) {
            parameter.setValue(std::move(value));
            return;
        }
    }
    throw std::invalid_argument("Cylinder 没有参数: " + name);
}

std::string CylinderFeature::validate() const
{
    // 领域对象自己再次保证半径和高度必须为正。
    for (const auto& parameter : parameters_) {
        if (parameter.asDouble() <= 0.0) {
            return "参数 " + parameter.name() + " 必须大于 0";
        }
    }
    return {};
}

TopoDS_Shape CylinderFeature::rebuild() const
{
    // 将普通参数交给几何层，由 ShapeFactory 封装 OCCT 构造调用。
    return geometry::ShapeFactory::makeCylinder(
        parameters_[0].asDouble(),
        parameters_[1].asDouble());
}

} // namespace forge::domain
