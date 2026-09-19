#include "domain/SphereFeature.h" // 球体领域对象的声明。

#include "geometry/ShapeFactory.h" // 封装实际 OCCT 球体创建过程。

#include <stdexcept> // 未知参数名使用 invalid_argument 报告。
#include <utility>   // std::move 转移 ID 和 ParameterValue。

namespace forge::domain {

SphereFeature::SphereFeature(std::string id, double radius)
    : Feature(std::move(id), "Sphere") // 基类保存实例 ID 和固定类型名。
    , parameters_{{"radius", radius}}  // 即使只有一个参数也沿用统一列表结构。
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
    // 统一按名字修改；即使当前只有一个参数，也保持与其他 Feature 相同的接口。
    for (auto& parameter : parameters_) {
        if (parameter.name() == name) {
            // 找到 radius 后替换 variant 中的当前值。
            parameter.setValue(std::move(value));
            return; // 修改成功，不需要继续遍历仅有的一项。
        }
    }
    // 当前球体只支持 radius，其他名字都属于调用协议错误。
    throw std::invalid_argument("Sphere 没有参数: " + name);
}

std::string SphereFeature::validate() const
{
    // radius 是球体成立的领域条件，不能只依赖 UI 输入框范围。
    if (parameters_[0].asDouble() <= 0.0) {
        // 球体半径为零或负数会退化，因此返回明确领域错误。
        return "参数 radius 必须大于 0";
    }
    return {}; // 空错误字符串代表球体状态合法。
}

TopoDS_Shape SphereFeature::rebuild() const
{
    // 每次按当前半径重新生成 Shape，显示层只接收最终 OCCT 对象。
    return geometry::ShapeFactory::makeSphere(parameters_[0].asDouble());
}

} // namespace forge::domain
