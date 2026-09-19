#pragma once // 防止 SphereFeature 类在同一编译单元重复定义。

#include "domain/Feature.h" // 提供抽象接口、公共身份和 Parameter 类型。

namespace forge::domain {

// 一个实际球体对象；当前只有 radius 一个参数。
class SphereFeature final : public Feature {
public:
    SphereFeature(std::string id, double radius); // 保存实例 ID 和初始半径。

    const std::vector<Parameter>& parameters() const override; // 只读返回半径列表。
    void setParameter(const std::string& name, ParameterValue value) override; // 修改半径。
    std::string validate() const override; // 半径大于零时返回空错误字符串。
    TopoDS_Shape rebuild() const override; // 按当前半径生成 OCCT 球体。

private:
    // 仍使用与其他 Feature 相同的参数容器，调用方无需为球体写特殊分支。
    std::vector<Parameter> parameters_;
};

} // namespace forge::domain
