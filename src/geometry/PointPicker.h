#pragma once
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <TopoDS_Shape.hxx>
#include <optional>
#include <vector>

namespace forge::geometry {
struct PickedPoint { gp_Pnt point; bool onSurface; };
// 优先选择射线前方最近的精确曲面交点；没有命中时投影到 Z=planeZ。
std::optional<PickedPoint> pickPoint(const gp_Pnt& origin, const gp_Dir& direction,
    const std::vector<TopoDS_Shape>& targets, double planeZ);
}
