#include "domain/SphereFeature.h" // 球体领域对象的声明。

#include "geometry/ShapeFactory.h" // 封装实际 OCCT 球体创建过程。

#include "domain/PositionParameters.h"

#include <stdexcept> // 未知参数名使用 invalid_argument 报告。
#include <utility>   // std::move 转移 ID 和 ParameterValue。

namespace forge::domain {

SphereFeature::SphereFeature(std::string id, double radius, double x, double y, double z)
    : Feature(std::move(id), "Sphere") // 基类保存实例 ID 和固定类型名。
    , parameters_{{"radius", radius}, {"x", x}, {"y", y}, {"z", z}}  // 尺寸在前，世界坐标位置在后。
{
    // 构造允许先形成对象，是否能进入文档由随后 validate 的结果决定。
}

const std::vector<Parameter>& SphereFeature::parameters() const
{
    // 返回当前球体参数的只读视图。
    return parameters_;
}

void SphereFeature::setParameter(const std::string& name, ParameterValue value)
{
    // 统一按名字修改；尺寸和位置沿用其他 Feature 相同的接口。
    for (auto& parameter : parameters_) {
        if (parameter.name() == name) {
            // 找到具名参数后替换当前值。
            parameter.setValue(std::move(value));
            return; // 参数名唯一，修改成功后立即返回。
        }
    }
    // 拒绝未定义的参数名。
    throw std::invalid_argument("Sphere 没有参数: " + name);
}

std::string SphereFeature::validate() const
{
    return validatePrimitiveParameters(parameters_);
}

TopoDS_Shape SphereFeature::rebuild(const std::vector<TopoDS_Shape>&) const
{
    // 每次按当前半径重新生成 Shape，显示层只接收最终 OCCT 对象。
    if (!validate().empty()) return {};
    // 先在局部坐标系构造，再应用世界坐标平移；布尔运算收到的已经是定位后的形状。
    return geometry::ShapeFactory::translate(
        geometry::ShapeFactory::makeSphere(parameters_[0].asDouble()),
        parameters_[1].asDouble(), parameters_[2].asDouble(), parameters_[3].asDouble());
}

} // namespace forge::domain
