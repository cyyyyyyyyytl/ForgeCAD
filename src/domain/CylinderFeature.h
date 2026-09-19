#pragma once // 防止 CylinderFeature 类被重复定义。

#include "domain/Feature.h" // 提供共同身份、虚函数接口和 Parameter 类型。

namespace forge::domain {

// 一个实际圆柱对象；半径和高度是它自己的实例数据。
class CylinderFeature final : public Feature {
public:
    // 参数顺序固定为 radius、height，与 Registry 和 rebuild 保持一致。
    CylinderFeature(std::string id, double radius, double height);

    const std::vector<Parameter>& parameters() const override; // 只读返回半径和高度。
    void setParameter(const std::string& name, ParameterValue value) override; // 按名称修改。
    std::string validate() const override; // 检查半径、高度都必须大于零。
    TopoDS_Shape rebuild() const override; // 用当前参数重新生成 OCCT 圆柱体。

private:
    // 参数名用于 UI/AI，vector 顺序用于稳定展示和几何重建。
    std::vector<Parameter> parameters_;
};

} // namespace forge::domain
