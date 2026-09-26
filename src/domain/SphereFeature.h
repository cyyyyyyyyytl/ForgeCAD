#pragma once // 防止 SphereFeature 类在同一编译单元重复定义。

#include "domain/Feature.h" // 提供抽象接口、公共身份和 Parameter 类型。

namespace forge::domain {

// 一个实际球体对象；半径和球心位置构成它的实例参数。
class SphereFeature final : public Feature {
public:
    SphereFeature(std::string id, double radius, double x = 0.0, double y = 0.0, double z = 0.0); // 保存实例 ID 和初始半径。

    const std::vector<Parameter>& parameters() const override; // 只读返回半径和位置。
    void setParameter(const std::string& name, ParameterValue value) override; // 修改尺寸或位置。
    std::string validate() const override; // 检查正半径与有限位置。
    TopoDS_Shape rebuild(const std::vector<TopoDS_Shape>& inputs = {}) const override; // 基础体暂不使用上游形状。

private:
    // 仍使用与其他 Feature 相同的参数容器，调用方无需为球体写特殊分支。
    std::vector<Parameter> parameters_;
};

} // namespace forge::domain
