#pragma once // ShapeFactory 声明会被多个 Feature 和测试包含，只展开一次。

#include <TopoDS_Shape.hxx> // 三个工厂函数统一返回的 OCCT 拓扑形状类型。

namespace forge::geometry {

// 通过 OCCT 创建基础几何体；上层不需要接触具体 BRepPrimAPI 构造器。
class ShapeFactory {
public:
    // 创建 X/Y/Z 尺寸分别为 length/width/height 的长方体；失败返回空 Shape。
    static TopoDS_Shape makeBox(double length, double width, double height);
    // 创建以 radius 为半径、height 为高度的圆柱；失败返回空 Shape。
    static TopoDS_Shape makeCylinder(double radius, double height);
    // 创建以 radius 为半径的球体；失败返回空 Shape。
    static TopoDS_Shape makeSphere(double radius);
};

} // namespace forge::geometry
