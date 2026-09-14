#pragma once

#include "domain/Feature.h"

namespace forge::domain {

// 一个实际球体对象；当前只有 radius 一个参数。
class SphereFeature final : public Feature {
public:
    SphereFeature(std::string id, double radius);

    const std::vector<Parameter>& parameters() const override;
    void setParameter(const std::string& name, ParameterValue value) override;
    std::string validate() const override;
    TopoDS_Shape rebuild() const override;

private:
    // 仍使用与其他 Feature 相同的参数容器，调用方无需为球体写特殊分支。
    std::vector<Parameter> parameters_;
};

} // namespace forge::domain
