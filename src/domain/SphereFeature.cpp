#include "domain/SphereFeature.h"

#include "geometry/ShapeFactory.h"

#include <stdexcept>
#include <utility>

namespace forge::domain {

SphereFeature::SphereFeature(std::string id, double radius)
    : Feature(std::move(id), "Sphere")
    , parameters_{{"radius", radius}}
{
}

const std::vector<Parameter>& SphereFeature::parameters() const
{
    // 返回当前球体参数的只读视图。
    return parameters_;
}

void SphereFeature::setParameter(const std::string& name, ParameterValue value)
{
    // 统一按名字修改；即使当前只有一个参数，也保持与其他 Feature 相同的接口。
    for (auto& parameter : parameters_) {
        if (parameter.name() == name) {
            parameter.setValue(std::move(value));
            return;
        }
    }
    throw std::invalid_argument("Sphere 没有参数: " + name);
}

std::string SphereFeature::validate() const
{
    // radius 是球体成立的领域条件，不能只依赖 UI 输入框范围。
    if (parameters_[0].asDouble() <= 0.0) {
        return "参数 radius 必须大于 0";
    }
    return {};
}

TopoDS_Shape SphereFeature::rebuild() const
{
    // 每次按当前半径重新生成 Shape，显示层只接收最终 OCCT 对象。
    return geometry::ShapeFactory::makeSphere(parameters_[0].asDouble());
}

} // namespace forge::domain
