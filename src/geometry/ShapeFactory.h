#pragma once // ShapeFactory 声明会被多个 Feature 和测试包含，只展开一次。

#include "core/ShapeResult.h"
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
    // 按世界坐标平移，保留精确拓扑；非法输入或内核失败返回空形状。
    static TopoDS_Shape translate(const TopoDS_Shape& shape, double x, double y, double z);
    // 检查非空形状的拓扑有效性；没有顶点的合法空结果与失败分开表示。
    static core::ShapeResult inspectShape(const TopoDS_Shape& shape);
    static core::ShapeResult differenceResult(const TopoDS_Shape& base, const TopoDS_Shape& tool);
    static core::ShapeResult unionResult(const TopoDS_Shape& base, const TopoDS_Shape& tool);
    static core::ShapeResult intersectionResult(const TopoDS_Shape& base, const TopoDS_Shape& tool);
    // 差集：从 base 中减去 tool；失败返回空 Shape。
    static TopoDS_Shape booleanDifference(const TopoDS_Shape& base, const TopoDS_Shape& tool);
    // 并集：合并 base 和 tool；失败返回空 Shape。
    static TopoDS_Shape booleanUnion(const TopoDS_Shape& base, const TopoDS_Shape& tool);
    // 交集：只保留 base 和 tool 重叠的部分；失败返回空 Shape。
    static TopoDS_Shape booleanIntersection(const TopoDS_Shape& base, const TopoDS_Shape& tool);
};

} // namespace forge::geometry
