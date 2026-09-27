#include "geometry/PointPicker.h"
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <gp_Lin.hxx>
#include <gp_Vec.hxx>
#include <cmath>
#include <limits>

namespace forge::geometry {
std::optional<PickedPoint> pickPoint(const gp_Pnt& origin, const gp_Dir& direction,
    const std::vector<TopoDS_Shape>& targets, double planeZ)
{
    if (!std::isfinite(origin.X()) || !std::isfinite(origin.Y()) || !std::isfinite(origin.Z()) ||
        !std::isfinite(planeZ)) return {};
    const gp_Lin ray(origin,direction);
    double nearest=std::numeric_limits<double>::infinity();
    std::optional<PickedPoint> result;
    for (const auto& shape : targets) {
        if (shape.IsNull()) continue;
        try {
            IntCurvesFace_ShapeIntersector intersector;
            intersector.Load(shape,Precision::Confusion());
            intersector.Perform(ray,0,1e12);
            if (!intersector.IsDone()) continue;
            for (int i=1;i<=intersector.NbPnt();++i) {
                const auto distance=intersector.WParameter(i);
                if (distance>=0 && distance<nearest) {
                    nearest=distance; result=PickedPoint{intersector.Pnt(i),true};
                }
            }
        } catch (const Standard_Failure&) { /* 单个不可拾取对象不影响其他目标。 */ }
    }
    if (result) return result;
    if (std::abs(direction.Z())<1e-10) return {};
    const auto distance=(planeZ-origin.Z())/direction.Z();
    if (!std::isfinite(distance) || distance<0) return {};
    return PickedPoint{origin.Translated(gp_Vec(direction)*distance),false};
}
}
