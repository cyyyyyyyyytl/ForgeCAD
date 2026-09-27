#include "domain/RectangleSketchFeature.h"
#include <utility>
#include "domain/PositionParameters.h"
#include <stdexcept>
#include "geometry/ShapeFactory.h"

namespace forge::domain {

    RectangleSketchFeature::RectangleSketchFeature(
        std::string id,
        double length,
        double width,
        double x,
        double y,
        double z)
        : Feature(std::move(id), "RectangleSketch")
        , parameters_{
        {"length", length},
        {"width", width},
        {"x", x},
        {"y", y},
        {"z", z}}
    {
    }

    const std::vector<Parameter>&
    RectangleSketchFeature::parameters() const
    {
        return parameters_;
    }

    void RectangleSketchFeature::setParameter(const std::string& name,ParameterValue value)
    {
        for (auto& parameter : parameters_) {
            if (parameter.name() == name) {
                parameter.setValue(std::move(value));
                return;
            }
        }

        throw std::invalid_argument("矩形草图没有参数: " + name);
    }

    std::string RectangleSketchFeature::validate() const
    {
        return validatePrimitiveParameters(parameters_);
    }

    TopoDS_Shape RectangleSketchFeature::rebuild(const std::vector<TopoDS_Shape>&) const
    {
        if (!validate().empty()) return {};

        const auto wire = geometry::ShapeFactory::makeRectangleWire(
            parameters_[0].asDouble(),  // length
            parameters_[1].asDouble()); // width

        return geometry::ShapeFactory::translate(
            wire,
            parameters_[2].asDouble(),  // x
            parameters_[3].asDouble(),  // y
            parameters_[4].asDouble()); // XY 平面的高度
    }
} // namespace forge::domain
