#pragma once
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopoDS_Shape.hxx>

namespace forge::geometry {

// 通过 OCCT 创建基础几何体
class ShapeFactory {
public:
    // 创建长方体，返回 OCCT shape；失败返回空TopoDS
    static TopoDS_Shape makeBox(double length, double width, double height);
};

} // namespace forge::geometry
