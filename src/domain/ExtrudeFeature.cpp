#include "domain/ExtrudeFeature.h"

#include <cmath>
#include <stdexcept>
#include <utility>
#include "geometry/ShapeFactory.h"
namespace forge::domain {

    ExtrudeFeature::ExtrudeFeature(std::string id, double height, ExtrudeDirection direction)
        : Feature(std::move(id), "Extrude")
        , parameters_{{"height", height}, {"direction", static_cast<double>(direction)}}
    {
    }

    const std::vector<Parameter>& ExtrudeFeature::parameters() const
    {
        return parameters_;
    }

    void ExtrudeFeature::setParameter(
        const std::string& name,
        ParameterValue value)
    {
        for (auto& parameter : parameters_) {
            if (parameter.name() == name) {
                parameter.setValue(std::move(value));
                return;
            }
        }
        throw std::invalid_argument("拉伸没有参数: " + name);
    }

    std::string ExtrudeFeature::validate() const
    {
        const double height = parameters_[0].asDouble();

        if (!std::isfinite(height) || height <= 0.0) {
            return "拉伸高度必须是有限的正数";
        }

        const double direction = parameters_[1].asDouble();
        if (direction != 0.0 && direction != 1.0 && direction != 2.0)
            return "拉伸方向必须是正向、反向或对称";
        return {};
    }
    TopoDS_Shape ExtrudeFeature::rebuild(const std::vector<TopoDS_Shape>& inputs) const
    {
        if (!validate().empty()) return {};

        // 第一版拉伸只接受一个上游草图。
        if (inputs.size() != 1 || inputs[0].IsNull()) {
            return {};
        }

        const double height = parameters_[0].asDouble();
        const double direction = parameters_[1].asDouble();
        const auto profile = direction == 2.0
            ? geometry::ShapeFactory::translate(inputs[0], 0, 0, -height / 2)
            : inputs[0];
        return geometry::ShapeFactory::extrudeWire(profile, direction == 1.0 ? -height : height);
    }
} // namespace forge::domain
