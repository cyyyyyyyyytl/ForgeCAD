#pragma once // 防止 CylinderFeature 类被重复定义。

#include "domain/Feature.h" // 提供共同身份、虚函数接口和 Parameter 类型。

namespace forge::domain {

// 一个实际圆柱对象；半径和高度是它自己的实例数据。
class CylinderFeature final : public Feature {
public:
    // 参数顺序固定为 radius、height，与 Registry 和 rebuild 保持一致。
    CylinderFeature(std::string id, double radius, double height, double x = 0.0, double y = 0.0, double z = 0.0);

    const std::vector<Parameter>& parameters() const override; // 只读返回尺寸和位置。
    void setParameter(const std::string& name, ParameterValue value) override; // 按名称修改。
    std::string validate() const override; // 检查正尺寸与有限位置。
    TopoDS_Shape rebuild(const std::vector<TopoDS_Shape>& inputs = {}) const override; // 基础体暂不使用上游形状。

private:
    // 参数名用于 UI/AI，vector 顺序用于稳定展示和几何重建。
    std::vector<Parameter> parameters_;
};

} // namespace forge::domain
