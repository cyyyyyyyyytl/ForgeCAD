#include "domain/ExtrudeFeature.h"

#include <cmath>
#include <stdexcept>
#include <utility>
#include <Standard_Failure.hxx>
#include "geometry/ShapeFactory.h"
#include "geometry/AdvancedModeling.h"
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
        return rebuildResult(inputs).shape;
    }
    core::ShapeResult ExtrudeFeature::rebuildResult(const std::vector<TopoDS_Shape>& inputs) const
    {
        const auto fail=[](std::string message)->core::ShapeResult{return {{},core::RebuildStatus::Failed,std::move(message)};};
        const auto error=validate();if (!error.empty()) return fail(error);
        try {

        // 第一版拉伸只接受一个上游草图。
        if (inputs.size() != 1 || inputs[0].IsNull() || inputs[0].ShapeType()!=TopAbs_WIRE) {
            return fail("拉伸需要闭合平面草图");
        }

        const double height = parameters_[0].asDouble();
        const double direction = parameters_[1].asDouble();
        const auto normal=geometry::AdvancedModeling::sketchNormal(inputs[0]);
        const auto profile = direction == 2.0
            ? geometry::ShapeFactory::translate(inputs[0], -normal.X()*height/2, -normal.Y()*height/2, -normal.Z()*height/2)
            : inputs[0];
        const auto shape=geometry::ShapeFactory::extrudeWire(profile, direction == 1.0 ? -height : height);
        if(shape.IsNull())return fail("无法拉伸，请检查闭合性、自交和平面");
        return geometry::ShapeFactory::inspectShape(shape);
        }catch(const Standard_Failure& e){return fail(e.GetMessageString()?e.GetMessageString():"拉伸失败");}
        catch(const std::exception& e){return fail(e.what());}
    }
} // namespace forge::domain
