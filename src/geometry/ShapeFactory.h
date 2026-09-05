#pragma once
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopoDS_Shape.hxx>

namespace forge::geometry {

// 通过 OCCT 创建基础几何体
class ShapeFactory {
public:
    // 创建长方体，返回 OCCT shape；失败返回空TopoDS
    static TopoDS_Shape makeBox(double length, double width, double height);
    // 创建圆柱（半径 + 高度）；失败返回空形状（规则与 makeBox 一致）
    static TopoDS_Shape makeCylinder(double radius, double height);
    // 创建球体（半径）；失败返回空形状（规则与其他形状一致）
    static TopoDS_Shape makeSphere(double radius);
};

} // namespace forge::geometry
