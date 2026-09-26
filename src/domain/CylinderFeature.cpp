#include "domain/CylinderFeature.h" // 圆柱领域对象的声明。

#include "geometry/ShapeFactory.h" // 负责实际调用 OCCT 构造圆柱体。

#include "domain/PositionParameters.h"

#include <stdexcept> // 未知参数名通过 invalid_argument 报告。
#include <utility>   // std::move 转移 ID 和 variant 参数值。

namespace forge::domain {

CylinderFeature::CylinderFeature(std::string id, double radius, double height, double x, double y, double z)
    : Feature(std::move(id), "Cylinder") // 基类保存稳定 ID 和类型名称。
    , parameters_{{"radius", radius},    // 下标 0 固定保存半径。
                  {"height", height}, {"x", x}, {"y", y}, {"z", z}}    // 下标 1 固定保存高度。
{
    // 这里只建立状态；Document 会在对象进入文档前调用 validate。
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
            // 命中目标参数后替换它的当前值。
            parameter.setValue(std::move(value));
            return; // 参数名唯一，成功后不必继续扫描。
        }
    }
    // 没找到表示外部请求了圆柱并不支持的参数。
    throw std::invalid_argument("Cylinder 没有参数: " + name);
}

std::string CylinderFeature::validate() const
{
    return validatePrimitiveParameters(parameters_);
}

TopoDS_Shape CylinderFeature::rebuild(const std::vector<TopoDS_Shape>&) const
{
    // 将普通参数交给几何层，由 ShapeFactory 封装 OCCT 构造调用。
    if (!validate().empty()) return {};
    // 先在局部坐标系构造，再应用世界坐标平移；布尔运算收到的已经是定位后的形状。
    return geometry::ShapeFactory::translate(
        geometry::ShapeFactory::makeCylinder(parameters_[0].asDouble(), parameters_[1].asDouble()),
        parameters_[2].asDouble(), parameters_[3].asDouble(), parameters_[4].asDouble());
}

} // namespace forge::domain
