#pragma once

#include "domain/Feature.h"

namespace forge::domain {

// 一个实际长方体对象。公共身份放在 Feature，Box 特有的长宽高放在这里。
class BoxFeature final : public Feature {
public:
    // 构造顺序固定为 length、width、height，与 Registry 的说明保持一致。
    BoxFeature(std::string id, double length, double width, double height);

    const std::vector<Parameter>& parameters() const override;
    void setParameter(const std::string& name, ParameterValue value) override;
    std::string validate() const override;
    TopoDS_Shape rebuild() const override;

private:
    // 使用有序 vector，既保持属性面板显示顺序，也便于 rebuild 按固定顺序取值。
    std::vector<Parameter> parameters_;
};

} // namespace forge::domain
