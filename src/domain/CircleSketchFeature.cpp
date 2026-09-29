#include "domain/CircleSketchFeature.h"
#include <utility>
#include "domain/PositionParameters.h"
#include <stdexcept>
#include "geometry/ShapeFactory.h"
#include "geometry/AdvancedModeling.h"

namespace forge::domain {

    CircleSketchFeature::CircleSketchFeature(
        std::string id,
        double radius,
        double x,
        double y,
        double z, int plane)
        : Feature(std::move(id), "CircleSketch")
        , parameters_{
        {"radius", radius},
        {"x", x},
        {"y", y},
        {"z", z}, {"plane",static_cast<double>(plane)}}
    {
    }

    const std::vector<Parameter>&
    CircleSketchFeature::parameters() const
    {
        return parameters_;
    }

    void CircleSketchFeature::setParameter(const std::string& name,ParameterValue value)
    {
        for (auto& parameter : parameters_) {
            if (parameter.name() == name) {
                parameter.setValue(std::move(value));
                return;
            }
        }

        throw std::invalid_argument("圆形草图没有参数: " + name);
    }

    std::string CircleSketchFeature::validate() const
    {
        return validatePrimitiveParameters(parameters_);
    }

    TopoDS_Shape CircleSketchFeature::rebuild(const std::vector<TopoDS_Shape>&) const
    {
        if (!validate().empty()) return {};

        const auto wire = geometry::ShapeFactory::makeCircleWire(
            parameters_[0].asDouble()); // radius

        return geometry::ShapeFactory::translate(
            geometry::AdvancedModeling::orientSketch(wire,static_cast<int>(parameters_[4].asDouble())),
            parameters_[1].asDouble(),  // x
            parameters_[2].asDouble(),  // y
            parameters_[3].asDouble()); // XY 平面的高度
    }
} // namespace forge::domain
