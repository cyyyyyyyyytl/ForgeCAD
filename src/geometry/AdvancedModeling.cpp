#include "geometry/AdvancedModeling.h"
#include "geometry/ShapeFactory.h"
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Wire.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_CompCurve.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax1.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GeomFill_Trihedron.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>
#include <Precision.hxx>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace forge::geometry {
namespace {
using ShapeMap=NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher>;
constexpr double tolerance=1e-6;
std::array<double,6> bounds(const TopoDS_Shape& shape)
{
    Bnd_Box box; BRepBndLib::AddOptimal(shape,box,false,false);
    if (box.IsVoid() || box.IsOpen()) throw std::invalid_argument("没有有限几何边界");
    std::array<double,6> b; box.Get(b[0],b[1],b[2],b[3],b[4],b[5]); return b;
}
TopoDS_Shape solid(const TopoDS_Shape& shape)
{
    if (!ShapeFactory::isSolidBody(shape)) throw std::invalid_argument("未生成有效实体，请检查输入、尺寸或曲线连接");
    GProp_GProps props; BRepGProp::VolumeProperties(shape,props,true,true,false);
    if (!std::isfinite(props.Mass()) || props.Mass()<=0) throw std::invalid_argument("实体体积非正");
    return shape;
}
TopoDS_Face face(const TopoDS_Shape& shape)
{
    if (shape.IsNull() || shape.ShapeType()!=TopAbs_WIRE || !TopoDS::Wire(shape).Closed())
        throw std::invalid_argument("截面必须是闭合平面轮廓");
    BRepBuilderAPI_MakeFace maker(TopoDS::Wire(shape),true);
    if (!maker.IsDone()) throw std::invalid_argument("无法填充轮廓，检查共面性和闭合性");
    TopoDS_Edge e1,e2;
    BRepCheck_Wire check(TopoDS::Wire(shape));
    if (check.SelfIntersect(maker.Face(),e1,e2)!=BRepCheck_NoError)
        throw std::invalid_argument("轮廓存在自交");
    if (!ShapeFactory::inspectShape(maker.Face()).usable()) throw std::invalid_argument("轮廓面无效");
    GProp_GProps props; BRepGProp::SurfaceProperties(maker.Face(),props);
    if (props.Mass()<=tolerance*tolerance) throw std::invalid_argument("轮廓面积为零或过小");
    return maker.Face();
}
const char* curveName(GeomAbs_CurveType type)
{
    switch(type) {
    case GeomAbs_Line:return "line"; case GeomAbs_Circle:return "circle";
    case GeomAbs_Ellipse:return "ellipse"; case GeomAbs_BSplineCurve:return "bspline";
    case GeomAbs_BezierCurve:return "bezier"; default:return "other";
    }
}
EdgeInfo edgeInfo(const TopoDS_Edge& edge,int index)
{
    EdgeInfo info; info.bounds=bounds(edge);
    BRepAdaptor_Curve curve(edge); info.curve=curveName(curve.GetType());
    GProp_GProps props; BRepGProp::LinearProperties(edge,props); info.length=props.Mass();
    // Geometry-bound IDs intentionally fail after geometry changes rather than silently select another edge.
    std::ostringstream text; text<<info.curve<<':'<<std::fixed<<std::setprecision(6)<<info.length;
    for (double v:info.bounds) text<<':'<<v;
    for (double fraction:{0.0,0.5,1.0}) {
        const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*fraction);
        text<<':'<<p.X()<<':'<<p.Y()<<':'<<p.Z();
    }
    std::uint64_t hash=14695981039346656037ull;
    for (unsigned char c:text.str()) { hash^=c; hash*=1099511628211ull; }
    std::ostringstream id; id<<'E'<<index<<':'<<std::hex<<hash; info.id=id.str(); return info;
}
}

TopoDS_Shape AdvancedModeling::cone(double bottom,double top,double height)
{
    if (bottom==top) return ShapeFactory::makeCylinder(bottom,height);
    BRepPrimAPI_MakeCone maker(bottom,top,height);
    maker.Build();
    if (!maker.IsDone()) throw std::invalid_argument("圆锥/圆台构造失败");
    return solid(maker.Shape());
}
TopoDS_Shape AdvancedModeling::orientSketch(const TopoDS_Shape& shape,int plane)
{
    if (plane==0) return shape;
    gp_Trsf trsf;
    if (plane==1) trsf.SetValues(1,0,0,0, 0,0,-1,0, 0,1,0,0); // UV = XZ, normal -Y.
    else if (plane==2) trsf.SetValues(0,0,1,0, 1,0,0,0, 0,1,0,0); // UV = YZ, normal +X.
    else throw std::invalid_argument("未知草图平面");
    return BRepBuilderAPI_Transform(shape,trsf,true).Shape();
}
int AdvancedModeling::sketchPlane(const TopoDS_Shape& shape)
{
    const auto b=bounds(shape);
    if (std::abs(b[5]-b[2])<tolerance) return 0;
    if (std::abs(b[4]-b[1])<tolerance) return 1;
    if (std::abs(b[3]-b[0])<tolerance) return 2;
    throw std::invalid_argument("此操作需要平行 XY/XZ/YZ 的平面草图");
}
gp_Dir AdvancedModeling::sketchNormal(const TopoDS_Shape& shape)
{
    switch(sketchPlane(shape)) { case 0:return gp_Dir(0,0,1); case 1:return gp_Dir(0,-1,0); default:return gp_Dir(1,0,0); }
}
TopoDS_Shape AdvancedModeling::profile(const std::vector<domain::ProfileVertex>& vertices,bool closed,int plane)
{
    if (vertices.size()<(closed?3u:2u) || vertices.size()>256) throw std::invalid_argument("轮廓顶点数量无效");
    BRepBuilderAPI_MakeWire maker;
    const auto count=closed ? vertices.size() : vertices.size()-1;
    for (std::size_t i=0;i<count;++i) {
        const auto& a=vertices[i]; const auto& b=vertices[(i+1)%vertices.size()];
        const gp_Pnt start(a.u,a.v,0), end(b.u,b.v,0);
        const auto chord=start.Distance(end);
        if (chord<tolerance) throw std::invalid_argument("轮廓相邻顶点重复或过近");
        if (std::abs(a.bulge)<1e-12) maker.Add(BRepBuilderAPI_MakeEdge(start,end).Edge());
        else {
            const gp_Pnt middle((a.u+b.u)/2+(b.v-a.v)*a.bulge/2,
                                (a.v+b.v)/2-(b.u-a.u)*a.bulge/2,0);
            GC_MakeArcOfCircle arc(start,middle,end);
            if (!arc.IsDone()) throw std::invalid_argument("轮廓圆弧构造失败");
            maker.Add(BRepBuilderAPI_MakeEdge(arc.Value()).Edge());
        }
    }
    if (!maker.IsDone()) throw std::invalid_argument("轮廓线段未连接");
    const auto wire=maker.Wire();
    if (closed) (void)face(wire);
    return orientSketch(wire,plane);
}
TopoDS_Shape AdvancedModeling::path(const std::vector<domain::PathPoint>& points,bool closed)
{
    if (points.size()<(closed?3u:2u) || points.size()>256) throw std::invalid_argument("路径点数量无效");
    BRepBuilderAPI_MakeWire maker;
    const auto count=closed ? points.size() : points.size()-1;
    for (std::size_t i=0;i<count;++i) {
        const auto& a=points[i]; const auto& b=points[(i+1)%points.size()];
        const gp_Pnt start(a.point[0],a.point[1],a.point[2]), end(b.point[0],b.point[1],b.point[2]);
        if (start.Distance(end)<tolerance) throw std::invalid_argument("路径相邻顶点重复或过近");
        if (!a.through) maker.Add(BRepBuilderAPI_MakeEdge(start,end).Edge());
        else {
            const auto& m=*a.through; GC_MakeArcOfCircle arc(start,gp_Pnt(m[0],m[1],m[2]),end);
            if (!arc.IsDone()) throw std::invalid_argument("路径圆弧的起点、经过点、终点无效或共线");
            maker.Add(BRepBuilderAPI_MakeEdge(arc.Value()).Edge());
        }
    }
    if (!maker.IsDone()) throw std::invalid_argument("无法生成连接路径"); return maker.Wire();
}
TopoDS_Shape AdvancedModeling::transform(const TopoDS_Shape& shape,const std::array<double,3>& angles,
    const std::array<double,3>& pivot,const std::array<double,3>& translation)
{
    TopoDS_Shape result=shape;
    const gp_Pnt origin(pivot[0],pivot[1],pivot[2]);
    const gp_Dir axes[]{gp_Dir(1,0,0),gp_Dir(0,1,0),gp_Dir(0,0,1)};
    for (int i=0;i<3;++i) if (angles[i]!=0) {
        gp_Trsf rotate; rotate.SetRotation(gp_Ax1(origin,axes[i]),angles[i]*std::numbers::pi/180);
        result=BRepBuilderAPI_Transform(result,rotate,true).Shape();
    }
    return ShapeFactory::translate(result,translation[0],translation[1],translation[2]);
}
std::vector<EdgeInfo> AdvancedModeling::edges(const TopoDS_Shape& shape)
{
    if (!ShapeFactory::isSolidBody(shape)) throw std::invalid_argument("选边需要有效实体");
    ShapeMap map; TopExp::MapShapes(shape,TopAbs_EDGE,map);
    std::vector<EdgeInfo> result;
    for (int i=1;i<=map.Extent();++i) {
        const auto edge=TopoDS::Edge(map(i));
        if (!BRep_Tool::Degenerated(edge)) result.push_back(edgeInfo(edge,i));
    }
    return result;
}
TopoDS_Shape AdvancedModeling::dressEdges(const TopoDS_Shape& base,bool chamfer,double size,int selection,
    const std::vector<std::string>& edgeIds)
{
    const auto infos=edges(base); const auto b=bounds(base);
    ShapeMap map; TopExp::MapShapes(base,TopAbs_EDGE,map);
    std::vector<TopoDS_Edge> selected;
    for (const auto& info:infos) {
        const auto& e=info.bounds;
        const bool use=selection==0 || (selection==1 && std::abs(e[2]-b[5])<tolerance && std::abs(e[5]-b[5])<tolerance) ||
            (selection==2 && std::abs(e[2]-b[2])<tolerance && std::abs(e[5]-b[2])<tolerance) ||
            (selection==3 && info.curve=="line" && std::abs(e[3]-e[0])<tolerance && std::abs(e[4]-e[1])<tolerance && e[5]-e[2]>tolerance) ||
            (selection==4 && std::find(edgeIds.begin(),edgeIds.end(),info.id)!=edgeIds.end());
        if (use) {
            const int index=std::stoi(info.id.substr(1,info.id.find(':')-1)); selected.push_back(TopoDS::Edge(map(index)));
        }
    }
    if (selected.empty() || (selection==4 && selected.size()!=edgeIds.size()))
        throw std::invalid_argument("没有匹配边或指定边ID已失效；查询 list_edges 后重新选择，或使用语义选边规则");
    if (chamfer) {
        BRepFilletAPI_MakeChamfer operation(base);
        for (const auto& edge:selected) operation.Add(size,edge);
        operation.Build(); if (!operation.IsDone()) throw std::invalid_argument("倒角失败，请减小距离或调整选边");
        return solid(operation.Shape());
    }
    BRepFilletAPI_MakeFillet operation(base);
    for (const auto& edge:selected) operation.Add(size,edge);
    operation.Build(); if (!operation.IsDone()) throw std::invalid_argument("圆角失败，请减小半径或调整选边");
    return solid(operation.Shape());
}
TopoDS_Shape AdvancedModeling::sweep(const TopoDS_Shape& profile,const TopoDS_Shape& path,bool align)
{
    auto section=face(profile);
    if (path.IsNull() || path.ShapeType()!=TopAbs_WIRE) throw std::invalid_argument("扫掠路径必须是连接线框");
    BRepAdaptor_CompCurve curve(TopoDS::Wire(path),true);
    gp_Pnt start; gp_Vec tangent; curve.D1(curve.FirstParameter(),start,tangent);
    if (tangent.Magnitude()<tolerance) throw std::invalid_argument("路径起点切线无效");
    GProp_GProps props; BRepGProp::SurfaceProperties(section,props);
    const auto center=props.CentreOfMass();
    const gp_Dir normal=BRepAdaptor_Surface(section).Plane().Axis().Direction();
    if (align) {
        gp_Trsf placement; placement.SetDisplacement(gp_Ax3(center,normal),gp_Ax3(start,gp_Dir(tangent)));
        section=TopoDS::Face(BRepBuilderAPI_Transform(section,placement,true).Shape());
    } else if (center.Distance(start)>tolerance || !normal.IsParallel(gp_Dir(tangent),tolerance))
        throw std::invalid_argument("截面中心必须在路径起点且法线平行起点切线；可启用 align_profile");
    BRepOffsetAPI_MakePipe pipe(TopoDS::Wire(path),section,GeomFill_IsCorrectedFrenet,true);
    if (!pipe.IsDone()) throw std::invalid_argument("扫掠失败，请检查路径连续性与截面尺寸");
    return solid(pipe.Shape());
}
TopoDS_Shape AdvancedModeling::loft(const std::vector<TopoDS_Shape>& profiles,bool ruled)
{
    BRepOffsetAPI_ThruSections operation(true,ruled,tolerance);
    operation.CheckCompatibility(true);
    for (const auto& profile:profiles) { (void)face(profile); operation.AddWire(TopoDS::Wire(profile)); }
    operation.Build(); if (!operation.IsDone()) throw std::invalid_argument("放样失败，检查截面顺序、位置和兼容性");
    return solid(operation.Shape());
}
}
