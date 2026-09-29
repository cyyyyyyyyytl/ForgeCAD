#include "domain/RevolveFeature.h"
#include "geometry/ShapeFactory.h"
#include "geometry/AdvancedModeling.h"
#include "domain/PositionParameters.h"
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>

namespace forge::domain {

    RevolveFeature::RevolveFeature(
        std::string id,
        double angle,
        RevolveAxis axis, double x, double y, double z)
        : Feature(std::move(id), "Revolve")
        , parameters_{
        {"angle", angle},
        {"axis", static_cast<double>(axis)}, {"x",x}, {"y",y}, {"z",z}}
    {
    }

    const std::vector<Parameter>&
    RevolveFeature::parameters() const
    {
        return parameters_;
    }

    void RevolveFeature::setParameter(
        const std::string& name,
        ParameterValue value)
    {
        for (auto& parameter : parameters_) {
            if (parameter.name() == name) {
                parameter.setValue(std::move(value));
                return;
            }
        }

        throw std::invalid_argument("旋转没有参数: " + name);
    }

    std::string RevolveFeature::validate() const
    {
        const double angle = parameters_[0].asDouble();
        if (!std::isfinite(angle) || angle < 0.001 || angle > 360.0) {
            return "旋转角度须在 0.001 到 360 度之间";
        }

        const double axis = parameters_[1].asDouble();
        if (axis != 0.0 && axis != 1.0 && axis != 2.0) {
            return "旋转轴必须是 X、Y 或 Z";
        }
        for (std::size_t i=2;i<parameters_.size();++i) {
            const auto value=parameters_[i].asDouble();
            if (!std::isfinite(value) || std::abs(value)>positionLimit)
                return "旋转结果平移必须是范围内的有限数值: " + parameters_[i].name();
        }

        return {};
    }
    TopoDS_Shape RevolveFeature::rebuild(const std::vector<TopoDS_Shape>& inputs) const
    {
        return rebuildResult(inputs).shape;
    }

    core::ShapeResult RevolveFeature::rebuildResult(const std::vector<TopoDS_Shape>& inputs) const
    {
        const auto fail=[](const std::string& message) -> core::ShapeResult {
            return {{},core::RebuildStatus::Failed,message};
        };
        const auto error=validate();
        if (!error.empty()) return fail(error);

        // 旋转需要一个上游草图。
        if (inputs.size() != 1 || inputs[0].IsNull()) {
            return fail("旋转需要恰好一个有效草图输入");
        }
        if (inputs[0].ShapeType()!=TopAbs_WIRE) return fail("旋转输入必须是闭合草图轮廓");

        const double angle = parameters_[0].asDouble();
        const double axisValue = parameters_[1].asDouble();

        // 当前草图平行于 XY；跨过径向零点会把两侧轮廓扫到同一区域。
        // 允许接触轴所在的边界，例如矩形从 X=0 起绕 Y 轴生成圆柱。
        try {
            Bnd_Box bounds;
            BRepBndLib::AddOptimal(inputs[0],bounds,false,false);
            if (bounds.IsVoid()) return fail("旋转草图没有有效边界");
            double xmin,ymin,zmin,xmax,ymax,zmax;
            bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
            const int plane=geometry::AdvancedModeling::sketchPlane(inputs[0]);
            if ((axisValue==0 && plane==2) || (axisValue==1 && plane==1) || (axisValue==2 && plane==0))
                return fail("Rotation axis must be parallel to sketch plane");
            const double minimum=axisValue==0 ? (plane==0?ymin:zmin) : axisValue==1 ? (plane==0?xmin:zmin) : (plane==1?xmin:ymin);
            const double maximum=axisValue==0 ? (plane==0?ymax:zmax) : axisValue==1 ? (plane==0?xmax:zmax) : (plane==1?xmax:ymax);
            if (minimum < -Precision::Confusion() && maximum > Precision::Confusion())
                return fail(axisValue==0.0 ? "草图跨越世界 X 轴的径向边界，请调整 Y 位置或轮廓尺寸" :
                    axisValue==1 ? "草图跨越世界 Y 轴的径向边界，请调整位置或轮廓尺寸" : "草图跨越世界 Z 轴的径向边界，请调整位置或轮廓尺寸");
        } catch (const Standard_Failure& failure) {
            return fail(failure.GetMessageString() ? failure.GetMessageString() : "旋转草图边界计算失败");
        } catch (const std::exception& failure) { return fail(failure.what()); }

        const gp_Dir direction = axisValue == 0.0
            ? gp_Dir(1, 0, 0)   // 世界 X 方向
            : axisValue==1 ? gp_Dir(0, 1, 0) : gp_Dir(0,0,1);  // 世界 Y 方向

        // 一条旋转轴由“经过的点 + 方向”确定。
        const gp_Ax1 axis(gp_Pnt(0, 0, 0), direction);

        auto result=geometry::ShapeFactory::inspectShape(
            geometry::ShapeFactory::revolveWire(inputs[0], axis, angle));
        if (!result.usable()) {
            result.message="无法生成有效旋转实体，请检查草图是否闭合、位置和旋转轴";
            return result;
        }
        // XYZ 是生成结果的世界平移量，与输入草图坐标及旋转轴定义分开。
        // 每次重建先旋转，再应用同一平移；重复编辑不会累计位移。
        const auto x=parameters_[2].asDouble(), y=parameters_[3].asDouble(), z=parameters_[4].asDouble();
        if (x!=0 || y!=0 || z!=0) {
            result=geometry::ShapeFactory::inspectShape(geometry::ShapeFactory::translate(result.shape,x,y,z));
            if (!result.usable()) result.message="旋转结果平移失败，请检查 XYZ 平移量";
        }
        return result;
    }

} // namespace forge::domain
