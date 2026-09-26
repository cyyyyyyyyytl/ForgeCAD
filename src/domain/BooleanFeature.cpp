#include "domain/BooleanFeature.h"

#include "geometry/ShapeFactory.h"

#include <stdexcept>
#include <utility>

namespace forge::domain {

namespace {

// Feature 用字符串表示类型；布尔特征内部用枚举避免拼错运算名称。
std::string typeName(BooleanOperation operation)
{
    switch (operation) {
    case BooleanOperation::Difference:
        return "Cut";
    case BooleanOperation::Union:
        return "Union";
    case BooleanOperation::Intersection:
        return "Intersection";
    }
    throw std::invalid_argument("未知布尔运算");
}

} // namespace

BooleanFeature::BooleanFeature(std::string id, BooleanOperation operation)
    : Feature(std::move(id), typeName(operation))
    , operation_(operation)
{
}

const std::vector<Parameter>& BooleanFeature::parameters() const
{
    return parameters_; // 布尔特征目前没有数值参数，返回空列表。
}

void BooleanFeature::setParameter(const std::string& name, ParameterValue)
{
    throw std::invalid_argument(type() + " 没有参数: " + name);
}

std::string BooleanFeature::validate() const
{
    return {}; // 枚举在构造时已校验；两个输入形状由 rebuild() 检查。
}

TopoDS_Shape BooleanFeature::rebuild(const std::vector<TopoDS_Shape>& inputs) const
{
    return rebuildResult(inputs).shape;
}

core::ShapeResult BooleanFeature::rebuildResult(const std::vector<TopoDS_Shape>& inputs) const
{
    if (inputs.size() != 2) return {{}, core::RebuildStatus::Failed, "布尔特征需要主体和工具两个输入"};
    switch (operation_) {
    case BooleanOperation::Difference: return geometry::ShapeFactory::differenceResult(inputs[0], inputs[1]);
    case BooleanOperation::Union: return geometry::ShapeFactory::unionResult(inputs[0], inputs[1]);
    case BooleanOperation::Intersection: return geometry::ShapeFactory::intersectionResult(inputs[0], inputs[1]);
    }
    return {{}, core::RebuildStatus::Failed, "未知布尔运算"};
}
} // namespace forge::domain
