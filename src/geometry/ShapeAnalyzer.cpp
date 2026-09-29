#include "geometry/ShapeAnalyzer.h"
#include "geometry/ShapeFactory.h"
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <cmath>
#include <stdexcept>

namespace forge::geometry {
namespace {
int count(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> map;
    TopExp::MapShapes(shape, type, map);
    return map.Extent(); // Shared edges/faces and repeated references are counted once.
}
void requireFinite(double value)
{
    if (!std::isfinite(value)) throw std::runtime_error("几何分析产生非有限数值");
}
}

ShapeAnalysis ShapeAnalyzer::analyze(const TopoDS_Shape& shape)
{
    const auto checked = ShapeFactory::inspectShape(shape);
    if (!checked.usable()) throw std::invalid_argument(checked.message);
    ShapeAnalysis result;
    if (checked.status == core::RebuildStatus::Empty) {
        result.volume = 0;
        return result;
    }
    result.empty = false;
    result.solids = count(shape, TopAbs_SOLID);
    result.faces = count(shape, TopAbs_FACE);
    result.edges = count(shape, TopAbs_EDGE);
    result.vertices = count(shape, TopAbs_VERTEX);
    result.solidBody = ShapeFactory::isSolidBody(shape);

    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    if (box.IsVoid() || box.IsOpen()) throw std::runtime_error("几何没有有限包围盒");
    std::array<double, 6> bounds;
    box.Get(bounds[0], bounds[1], bounds[2], bounds[3], bounds[4], bounds[5]);
    for (double value : bounds) requireFinite(value);
    result.bounds = bounds;

    GProp_GProps surface;
    BRepGProp::SurfaceProperties(shape, surface, true, false);
    result.area = surface.Mass();
    requireFinite(result.area);
    // A sketch or mixed compound has no meaningful solid volume/volume centroid.
    if (result.solidBody) {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(shape, properties, true, true, false);
        result.volume = properties.Mass();
        requireFinite(*result.volume);
        if (*result.volume > 0) {
            const auto center = properties.CentreOfMass();
            std::array<double, 3> xyz{center.X(), center.Y(), center.Z()};
            for (double value : xyz) requireFinite(value);
            result.volumeCentroid = xyz;
        }
    }
    return result;
}
} // namespace forge::geometry
