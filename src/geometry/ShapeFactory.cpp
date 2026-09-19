#include "geometry/ShapeFactory.h"      // 工厂函数声明和 TopoDS_Shape 类型。

#include <spdlog/spdlog.h>              // 记录非法参数、OCCT 异常和成功构造信息。
#include <Standard_Failure.hxx>          // OCCT 所有标准异常的共同基类。
#include <BRepPrimAPI_MakeBox.hxx>       // OCCT 长方体构造器，只在实现文件暴露。
#include <BRepPrimAPI_MakeCylinder.hxx>  // OCCT 圆柱体构造器。
#include <BRepPrimAPI_MakeSphere.hxx>    // OCCT 球体构造器。

namespace forge::geometry {

// ------------------------------------------------------------
// makeBox：用 OCCT 创建以原点为起点、沿 XYZ 正方向延伸的长方体
//   length/width/height 分别对应 X/Y/Z 尺寸。
//   ShapeFactory 不抛出几何异常给 UI，而是记录日志并返回空 TopoDS_Shape；
//   上层可用 IsNull() 统一判断几何构造是否成功。
// ------------------------------------------------------------
TopoDS_Shape ShapeFactory::makeBox(double length, double width, double height)
{
    // 先在进入 OCCT 前拒绝非正尺寸，减少内核异常和无意义计算。
    if (length <= 0 || width <= 0 || height <= 0) {
        spdlog::error("ShapeFactory::makeBox invalid dims: {}x{}x{}", length, width, height);
        return TopoDS_Shape(); // 默认构造的 TopoDS_Shape 为 IsNull()==true 的空形状。
    }
    try {
        // maker 是栈对象，离开作用域后自动析构；Shape() 返回 OCCT 拓扑形状句柄。
        BRepPrimAPI_MakeBox maker(length, width, height);
        // OCCT 8 的 IsDone() 偶发未置位，以 Shape() 是否成功且有效为准
        if (!maker.IsDone()) {
            // 仍尝试取形状，Shape() 失败会抛异常
            auto s = maker.Shape(); // 即使标志异常，也以实际能否取得形状作为最终依据。
            if (s.IsNull()) {
                spdlog::error("ShapeFactory::makeBox IsDone=false and Shape null");
                return TopoDS_Shape(); // 标志与实体都失败，向上层返回统一空值。
            }
            spdlog::warn("ShapeFactory::makeBox IsDone flag false but shape valid");
            return s; // 虽然标志异常，但实体有效，保留可用结果。
        }
        // 正常路径取出构造结果，并记录成功尺寸便于排查用户输入。
        auto s = maker.Shape(); // 正常完成后取得构造器生成的拓扑实体。
        if (!s.IsNull()) {
            spdlog::info("ShapeFactory::makeBox created {}x{}x{}", length, width, height);
        }
        return s; // 有效或空值都原样返回，由调用方统一使用 IsNull 判断。
    } catch (const Standard_Failure& e) {
        // Standard_Failure 是 OCCT 自己的异常基类，优先保留内核提供的诊断消息。
        spdlog::error("ShapeFactory::makeBox OCCT exception: {}",
                      e.GetMessageString() ? e.GetMessageString() : "unknown");
        return TopoDS_Shape(); // 将 OCCT 异常转换为项目统一的空形状失败协议。
    } catch (...) {
        // 最后一层兜底，避免未知异常越过几何层导致 Qt 事件循环退出。
        spdlog::error("ShapeFactory::makeBox unknown exception");
        return TopoDS_Shape(); // 捕获未知异常后同样返回空形状，保护 GUI 事件循环。
    }
}

// ------------------------------------------------------------
// makeCylinder：造圆柱（结构完全照抄 makeBox——同一套错误处理策略）
//   - 非法尺寸（半径/高度 <= 0）：记日志 + 返回空形状
//   - OCCT 异常：统一转成空形状，不让异常漏到上层
//   - IsDone 怪癖：标志不可信，以 Shape() 实际产出为准
// ------------------------------------------------------------
TopoDS_Shape ShapeFactory::makeCylinder(double radius, double height)
{
    // 圆柱半径或高度非正都会形成退化几何，进入 OCCT 前直接拒绝。
    if (radius <= 0 || height <= 0) {
        spdlog::error("ShapeFactory::makeCylinder invalid dims: radius={} height={}", radius, height);
        return TopoDS_Shape(); // 用空形状表示可预期的参数失败。
    }
    try {
        // 栈上构造器在函数退出时自动析构，内部 OCCT 资源由 Handle 管理。
        BRepPrimAPI_MakeCylinder maker(radius, height);
        // OCCT 8 的 IsDone() 偶发未置位，以 Shape() 是否成功且有效为准
        if (!maker.IsDone()) {
            // 仍尝试取形状，Shape() 失败会抛异常
            auto s = maker.Shape(); // IsDone 不可靠时继续检查实际产物。
            if (s.IsNull()) {
                spdlog::error("ShapeFactory::makeCylinder IsDone=false and Shape null");
                return TopoDS_Shape(); // 没有实际形状，确认构造失败。
            }
            spdlog::warn("ShapeFactory::makeCylinder IsDone flag false but shape valid");
            return s; // 保留 IsDone 标志异常但几何有效的结果。
        }
        auto s = maker.Shape(); // 正常路径取得圆柱 TopoDS_Shape。
        if (!s.IsNull()) {
            spdlog::info("ShapeFactory::makeCylinder created radius={} height={}", radius, height);
        }
        return s; // 把形状值交还给 Feature::rebuild 调用者。
    } catch (const Standard_Failure& e) {
        // OCCT 异常可能携带空消息指针，所以日志表达式提供 unknown 兜底。
        spdlog::error("ShapeFactory::makeCylinder OCCT exception: {}",
                      e.GetMessageString() ? e.GetMessageString() : "unknown");
        return TopoDS_Shape(); // 异常不越过 geometry 层。
    } catch (...) {
        // 捕获非 Standard_Failure 的意外异常，防止桌面应用崩溃。
        spdlog::error("ShapeFactory::makeCylinder unknown exception");
        return TopoDS_Shape(); // 仍遵循空形状失败协议。
    }
}

// ------------------------------------------------------------
// makeSphere：造球体（球心在原点，结构与其他形状同一套错误处理策略）
//   - 非法尺寸（半径 <= 0）：记日志 + 返回空形状
//   - OCCT 异常：统一转成空形状，不让异常漏到上层
//   - IsDone 怪癖：标志不可信，以 Shape() 实际产出为准
// ------------------------------------------------------------
TopoDS_Shape ShapeFactory::makeSphere(double radius)
{
    // 球体只有半径一个维度，非正值会产生退化形状。
    if (radius <= 0) {
        spdlog::error("ShapeFactory::makeSphere invalid dims: radius={}", radius);
        return TopoDS_Shape(); // 参数非法时不调用 OCCT。
    }
    try {
        // 使用 OCCT 基础体构造器按给定半径生成球体。
        BRepPrimAPI_MakeSphere maker(radius);
        // OCCT 8 的 IsDone() 偶发未置位，以 Shape() 是否成功且有效为准
        if (!maker.IsDone()) {
            // 仍尝试取形状，Shape() 失败会抛异常
            auto s = maker.Shape(); // 标志异常时继续以实际形状为准。
            if (s.IsNull()) {
                spdlog::error("ShapeFactory::makeSphere IsDone=false and Shape null");
                return TopoDS_Shape(); // 实体同样为空，确认失败。
            }
            spdlog::warn("ShapeFactory::makeSphere IsDone flag false but shape valid");
            return s; // 返回虽然标志异常但实际有效的球体。
        }
        auto s = maker.Shape(); // 正常路径取得球体拓扑形状。
        if (!s.IsNull()) {
            spdlog::info("ShapeFactory::makeSphere created radius={}", radius);
        }
        return s; // 交给上层显示或进一步建模。
    } catch (const Standard_Failure& e) {
        // 保留 OCCT 原始错误文字，便于诊断内核失败。
        spdlog::error("ShapeFactory::makeSphere OCCT exception: {}",
                      e.GetMessageString() ? e.GetMessageString() : "unknown");
        return TopoDS_Shape(); // 对上层隐藏异常，改用空值协议。
    } catch (...) {
        // 兜住第三方库之外的未知异常。
        spdlog::error("ShapeFactory::makeSphere unknown exception");
        return TopoDS_Shape(); // 防止异常穿透到 Qt 事件循环。
    }
}
} // namespace forge::geometry
