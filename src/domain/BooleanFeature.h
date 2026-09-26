#pragma once

#include "domain/Feature.h"

namespace forge::domain {

// 选择要对两个上游形状执行的运算；类型名由构造函数转换成 Cut/Union/Intersection。
enum class BooleanOperation { Difference, Union, Intersection };

// 上游 Feature 的 ID 存在依赖图中；本对象只保存运算种类。
class BooleanFeature final : public Feature {
public:
    BooleanFeature(std::string id, BooleanOperation operation);

    const std::vector<Parameter>& parameters() const override;
    void setParameter(const std::string& name, ParameterValue value) override;
    std::string validate() const override;
    // inputs 必须恰好有两个形状：先 base，后 tool；输入无效时返回空 Shape。
    TopoDS_Shape rebuild(const std::vector<TopoDS_Shape>& inputs = {}) const override;

    core::ShapeResult rebuildResult(const std::vector<TopoDS_Shape>& inputs = {}) const override;

private:
    BooleanOperation operation_;
    std::vector<Parameter> parameters_; // 布尔特征目前没有数值参数。
};

} // namespace forge::domain
