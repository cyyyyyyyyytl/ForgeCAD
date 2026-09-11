#include "geometry/ShapeFactory.h"
#include <spdlog/spdlog.h>
#include <Standard_Failure.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>

namespace forge::geometry {

// ------------------------------------------------------------
// makeBox：用 OCCT 创建以原点为起点、沿 XYZ 正方向延伸的长方体
//   length/width/height 分别对应 X/Y/Z 尺寸。
//   ShapeFactory 不抛出几何异常给 UI，而是记录日志并返回空 TopoDS_Shape；
//   上层可用 IsNull() 统一判断几何构造是否成功。
// ------------------------------------------------------------
TopoDS_Shape ShapeFactory::makeBox(double length, double width, double height) {
    // 先在进入 OCCT 前拒绝非正尺寸，减少内核异常和无意义计算。
    if (length <= 0 || width <= 0 || height <= 0) {
        spdlog::error("ShapeFactory::makeBox invalid dims: {}x{}x{}", length, width, height);
        return TopoDS_Shape();
    }
    try {
        // maker 是栈对象，离开作用域后自动析构；Shape() 返回 OCCT 拓扑形状句柄。
        BRepPrimAPI_MakeBox maker(length, width, height);
        // OCCT 8 的 IsDone() 偶发未置位，以 Shape() 是否成功且有效为准
        if (!maker.IsDone()) {
            // 仍尝试取形状，Shape() 失败会抛异常
            auto s = maker.Shape();
            if (s.IsNull()) {
                spdlog::error("ShapeFactory::makeBox IsDone=false and Shape null");
                return TopoDS_Shape();
            }
            spdlog::warn("ShapeFactory::makeBox IsDone flag false but shape valid");
            return s;
        }
        // 正常路径取出构造结果，并记录成功尺寸便于排查用户输入。
        auto s = maker.Shape();
        if (!s.IsNull()) {
            spdlog::info("ShapeFactory::makeBox created {}x{}x{}", length, width, height);
        }
        return s;
    } catch (const Standard_Failure& e) {
        // Standard_Failure 是 OCCT 自己的异常基类，优先保留内核提供的诊断消息。
        spdlog::error("ShapeFactory::makeBox OCCT exception: {}",
                      e.GetMessageString() ? e.GetMessageString() : "unknown");
        return TopoDS_Shape();
    } catch (...) {
        // 最后一层兜底，避免未知异常越过几何层导致 Qt 事件循环退出。
        spdlog::error("ShapeFactory::makeBox unknown exception");
        return TopoDS_Shape();
    }
}

// ------------------------------------------------------------
// makeCylinder：造圆柱（结构完全照抄 makeBox——同一套错误处理策略）
//   - 非法尺寸（半径/高度 <= 0）：记日志 + 返回空形状
//   - OCCT 异常：统一转成空形状，不让异常漏到上层
//   - IsDone 怪癖：标志不可信，以 Shape() 实际产出为准
// ------------------------------------------------------------
TopoDS_Shape ShapeFactory::makeCylinder(double radius, double height) {
    if (radius <= 0 || height <= 0) {
        spdlog::error("ShapeFactory::makeCylinder invalid dims: radius={} height={}", radius, height);
        return TopoDS_Shape();
    }
    try {
        BRepPrimAPI_MakeCylinder maker(radius, height);
        // OCCT 8 的 IsDone() 偶发未置位，以 Shape() 是否成功且有效为准
        if (!maker.IsDone()) {
            // 仍尝试取形状，Shape() 失败会抛异常
            auto s = maker.Shape();
            if (s.IsNull()) {
                spdlog::error("ShapeFactory::makeCylinder IsDone=false and Shape null");
                return TopoDS_Shape();
            }
            spdlog::warn("ShapeFactory::makeCylinder IsDone flag false but shape valid");
            return s;
        }
        auto s = maker.Shape();
        if (!s.IsNull()) {
            spdlog::info("ShapeFactory::makeCylinder created radius={} height={}", radius, height);
        }
        return s;
    } catch (const Standard_Failure& e) {
        spdlog::error("ShapeFactory::makeCylinder OCCT exception: {}",
                      e.GetMessageString() ? e.GetMessageString() : "unknown");
        return TopoDS_Shape();
    } catch (...) {
        spdlog::error("ShapeFactory::makeCylinder unknown exception");
        return TopoDS_Shape();
    }
}

// ------------------------------------------------------------
// makeSphere：造球体（球心在原点，结构与其他形状同一套错误处理策略）
//   - 非法尺寸（半径 <= 0）：记日志 + 返回空形状
//   - OCCT 异常：统一转成空形状，不让异常漏到上层
//   - IsDone 怪癖：标志不可信，以 Shape() 实际产出为准
// ------------------------------------------------------------
TopoDS_Shape ShapeFactory::makeSphere(double radius) {
    if (radius <= 0 ) {
        spdlog::error("ShapeFactory::makeSphere invalid dims: radius={}", radius);
        return TopoDS_Shape();
    }
    try {
        BRepPrimAPI_MakeSphere maker(radius);
        // OCCT 8 的 IsDone() 偶发未置位，以 Shape() 是否成功且有效为准
        if (!maker.IsDone()) {
            // 仍尝试取形状，Shape() 失败会抛异常
            auto s = maker.Shape();
            if (s.IsNull()) {
                spdlog::error("ShapeFactory::makeSphere IsDone=false and Shape null");
                return TopoDS_Shape();
            }
            spdlog::warn("ShapeFactory::makeSphere IsDone flag false but shape valid");
            return s;
        }
        auto s = maker.Shape();
        if (!s.IsNull()) {
            spdlog::info("ShapeFactory::makeSphere created radius={}", radius);
        }
        return s;
    } catch (const Standard_Failure& e) {
        spdlog::error("ShapeFactory::makeSphere OCCT exception: {}",
                      e.GetMessageString() ? e.GetMessageString() : "unknown");
        return TopoDS_Shape();
    } catch (...) {
        spdlog::error("ShapeFactory::makeSphere unknown exception");
        return TopoDS_Shape();
    }
}
} // namespace forge::geometry
