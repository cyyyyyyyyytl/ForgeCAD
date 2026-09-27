#include "domain/ExtrudeCutFeature.h"
#include "geometry/ShapeFactory.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <cmath>
#include <utility>

namespace forge::domain {
ExtrudeCutFeature::ExtrudeCutFeature(std::string id, double height, ExtrudeDirection direction)
    : Feature(id,"ExtrudeCut"), extrusion_(std::move(id),height,direction) {}

const std::vector<Parameter>& ExtrudeCutFeature::parameters() const { return extrusion_.parameters(); }
void ExtrudeCutFeature::setParameter(const std::string& name, ParameterValue value) {
    extrusion_.setParameter(name,std::move(value));
}
std::string ExtrudeCutFeature::validate() const { return extrusion_.validate(); }
TopoDS_Shape ExtrudeCutFeature::rebuild(const std::vector<TopoDS_Shape>& inputs) const {
    return rebuildResult(inputs).shape;
}
core::ShapeResult ExtrudeCutFeature::rebuildResult(const std::vector<TopoDS_Shape>& inputs) const {
    const auto error=validate();
    if (!error.empty()) return {{},core::RebuildStatus::Failed,error};
    if (inputs.size()!=2 || !geometry::ShapeFactory::isSolidBody(inputs[0]))
        return {{},core::RebuildStatus::Failed,"拉伸切除需要实体主体和一个草图输入"};
    const auto tool=extrusion_.rebuild({inputs[1]});
    if (tool.IsNull()) return {{},core::RebuildStatus::Failed,"切除轮廓无法生成拉伸实体"};
    const auto common=geometry::ShapeFactory::intersectionResult(inputs[0],tool);
    if (!common.usable()) return common;
    try {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(common.shape,properties);
        const auto volume=properties.Mass();
        if (!std::isfinite(volume) || volume<=std::pow(Precision::Confusion(),3))
            return {{},core::RebuildStatus::Failed,"切除区域与主体没有体积重叠，请检查草图位置、方向和高度"};
        return geometry::ShapeFactory::differenceResult(inputs[0],tool);
    } catch (const Standard_Failure&) {
        return {{},core::RebuildStatus::Failed,"无法计算切除区域"};
    }
}
}
