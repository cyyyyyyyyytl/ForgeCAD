#pragma once

#include "domain/Feature.h"

namespace forge::domain {

// 一个实际圆柱对象；半径和高度是它自己的实例数据。
class CylinderFeature final : public Feature {
public:
    // 参数顺序固定为 radius、height，与 Registry 和 rebuild 保持一致。
    CylinderFeature(std::string id, double radius, double height);

    const std::vector<Parameter>& parameters() const override;
    void setParameter(const std::string& name, ParameterValue value) override;
    std::string validate() const override;
    TopoDS_Shape rebuild() const override;

private:
    // 参数名用于 UI/AI，vector 顺序用于稳定展示和几何重建。
    std::vector<Parameter> parameters_;
};

} // namespace forge::domain
