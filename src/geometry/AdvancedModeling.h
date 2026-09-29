#pragma once
#include "domain/FeatureDefinition.h"
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <array>
#include <string>
#include <vector>

namespace forge::geometry {
struct EdgeInfo {
    std::string id, curve;
    double length=0;
    std::array<double,6> bounds{};
};
class AdvancedModeling {
public:
    static TopoDS_Shape cone(double bottom,double top,double height);
    static TopoDS_Shape orientSketch(const TopoDS_Shape& shape,int plane);
    static int sketchPlane(const TopoDS_Shape& shape);
    static gp_Dir sketchNormal(const TopoDS_Shape& shape);
    static TopoDS_Shape profile(const std::vector<domain::ProfileVertex>& vertices,bool closed,int plane);
    static TopoDS_Shape path(const std::vector<domain::PathPoint>& points,bool closed);
    static TopoDS_Shape transform(const TopoDS_Shape& shape,const std::array<double,3>& angles,
        const std::array<double,3>& pivot,const std::array<double,3>& translation);
    static std::vector<EdgeInfo> edges(const TopoDS_Shape& shape);
    static TopoDS_Shape dressEdges(const TopoDS_Shape& base,bool chamfer,double size,int selection,
        const std::vector<std::string>& edgeIds);
    static TopoDS_Shape sweep(const TopoDS_Shape& profile,const TopoDS_Shape& path,bool align);
    static TopoDS_Shape loft(const std::vector<TopoDS_Shape>& profiles,bool ruled);
};
}
