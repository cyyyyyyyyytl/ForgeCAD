#pragma once // 防止 BoxFeature 类被重复定义。

#include "domain/Feature.h" // BoxFeature 继承公共 Feature 接口并使用 Parameter。

namespace forge::domain {

// 一个实际长方体对象。公共身份放在 Feature，Box 特有的长宽高放在这里。
class BoxFeature final : public Feature {
public:
    // 构造顺序固定为 length、width、height，与 Registry 的说明保持一致。
    BoxFeature(std::string id, double length, double width, double height, double x = 0.0, double y = 0.0, double z = 0.0);

    const std::vector<Parameter>& parameters() const override; // 只读返回尺寸和位置。
    void setParameter(const std::string& name, ParameterValue value) override; // 按名字修改。
    std::string validate() const override;   // 检查正尺寸与有限位置。
    TopoDS_Shape rebuild(const std::vector<TopoDS_Shape>& inputs = {}) const override; // 基础体暂不使用上游形状。

private:
    // 使用有序 vector，既保持属性面板显示顺序，也便于 rebuild 按固定顺序取值。
    std::vector<Parameter> parameters_;
};

} // namespace forge::domain
