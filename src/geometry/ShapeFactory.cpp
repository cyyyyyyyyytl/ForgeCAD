#include "geometry/ShapeFactory.h"      // 工厂函数声明和 TopoDS_Shape 类型。

#include <BRepCheck_Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <sstream>
#include <utility>
#include <cmath>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopLoc_Location.hxx>

#include <spdlog/spdlog.h>              // 记录非法参数、OCCT 异常和成功构造信息。
#include <Standard_Failure.hxx>          // OCCT 所有标准异常的共同基类。
#include <BRepPrimAPI_MakeBox.hxx>       // OCCT 长方体构造器，只在实现文件暴露。
#include <BRepPrimAPI_MakeCylinder.hxx>  // OCCT 圆柱体构造器。
#include <BRepPrimAPI_MakeSphere.hxx>    // OCCT 球体构造器。
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>

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

TopoDS_Shape ShapeFactory::translate(const TopoDS_Shape& shape, double x, double y, double z)
{
    if (shape.IsNull() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return {};
    try {
        if (x == 0.0 && y == 0.0 && z == 0.0) return shape;
        gp_Trsf transform;
        transform.SetTranslation(gp_Vec(x, y, z));
        // 刚体平移使用 Location，保留几何共享，无需复制或重新离散化实体。
        return shape.Moved(TopLoc_Location(transform));
    } catch (const Standard_Failure& error) {
        spdlog::error("ShapeFactory::translate OCCT exception: {}",
                      error.GetMessageString() ? error.GetMessageString() : "unknown");
        return {};
    }
}


core::ShapeResult ShapeFactory::inspectShape(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return {{}, core::RebuildStatus::Failed, "几何构造失败：没有生成形状"};
    try {
        if (!BRepCheck_Analyzer(shape).IsValid()) return {{}, core::RebuildStatus::Failed, "几何形状无效：拓扑检查未通过"};
        if (!TopExp_Explorer(shape, TopAbs_VERTEX).More()) {
            return {shape, core::RebuildStatus::Empty, "运算完成，结果为空"};
        }
        return {shape, core::RebuildStatus::Ready, "重建成功"};
    } catch (const Standard_Failure& error) {
        return {{}, core::RebuildStatus::Failed, std::string("形状检查异常：") +
            (error.GetMessageString() ? error.GetMessageString() : "未知原因")};
    } catch (const std::exception& error) {
        return {{}, core::RebuildStatus::Failed, std::string("形状检查异常：") + error.what()};
    }
}

namespace {
core::ShapeResult emptyResult()
{
    TopoDS_Compound empty;
    BRep_Builder builder;
    builder.MakeCompound(empty);
    return {empty, core::RebuildStatus::Empty, "运算完成，结果为空"};
}

template<class Operation>
core::ShapeResult runBoolean(const TopoDS_Shape& base, const TopoDS_Shape& tool)
{
    try {
        const auto first = ShapeFactory::inspectShape(base);
        const auto second = ShapeFactory::inspectShape(tool);
        if (!first.usable()) return {{}, core::RebuildStatus::Failed, "主体输入无效：" + first.message};
        if (!second.usable()) return {{}, core::RebuildStatus::Failed, "工具输入无效：" + second.message};
        // 合法空集合的运算规则由调用方处理；不把空 Compound 送入内核。
        Operation operation(base, tool);
        if (!operation.IsDone() || operation.HasErrors()) {
            std::ostringstream details;
            operation.DumpErrors(details);
            return {{}, core::RebuildStatus::Failed, "布尔计算失败：" +
                (details.str().empty() ? std::string("内核未完成运算") : details.str())};
        }
        return ShapeFactory::inspectShape(operation.Shape());
    } catch (const Standard_Failure& error) {
        return {{}, core::RebuildStatus::Failed, std::string("几何内核异常：") +
            (error.GetMessageString() ? error.GetMessageString() : "未知原因")};
    } catch (const std::exception& error) {
        return {{}, core::RebuildStatus::Failed, std::string("几何计算异常：") + error.what()};
    }
}
// 在判断空集合前检查两个输入，防止“一个输入为空”掩盖另一个输入损坏。
std::pair<core::ShapeResult, core::ShapeResult> inspectInputs(const TopoDS_Shape& base, const TopoDS_Shape& tool)
{
    return {ShapeFactory::inspectShape(base), ShapeFactory::inspectShape(tool)};
}
}

core::ShapeResult ShapeFactory::differenceResult(const TopoDS_Shape& base, const TopoDS_Shape& tool)
{
    const auto [first, second] = inspectInputs(base, tool);
    if (!first.usable() || !second.usable()) return runBoolean<BRepAlgoAPI_Cut>(base, tool);
    if (first.status == core::RebuildStatus::Empty) return emptyResult();
    if (second.status == core::RebuildStatus::Empty) return first;
    auto result = runBoolean<BRepAlgoAPI_Cut>(base, tool);
    if (result.status == core::RebuildStatus::Empty) result.message = "差集完成，主体已被完全减去";
    return result;
}
core::ShapeResult ShapeFactory::unionResult(const TopoDS_Shape& base, const TopoDS_Shape& tool)
{
    const auto [first, second] = inspectInputs(base, tool);
    if (!first.usable() || !second.usable()) return runBoolean<BRepAlgoAPI_Fuse>(base, tool);
    if (first.status == core::RebuildStatus::Empty) return second;
    if (second.status == core::RebuildStatus::Empty) return first;
    return runBoolean<BRepAlgoAPI_Fuse>(base, tool);
}
core::ShapeResult ShapeFactory::intersectionResult(const TopoDS_Shape& base, const TopoDS_Shape& tool)
{
    const auto [first, second] = inspectInputs(base, tool);
    if (!first.usable() || !second.usable()) return runBoolean<BRepAlgoAPI_Common>(base, tool);
    auto result = (first.status == core::RebuildStatus::Empty || second.status == core::RebuildStatus::Empty)
        ? emptyResult() : runBoolean<BRepAlgoAPI_Common>(base, tool);
    if (result.status == core::RebuildStatus::Empty) result.message = "交集完成，没有共同几何，结果为空";
    return result;
}

TopoDS_Shape ShapeFactory::booleanDifference(const TopoDS_Shape& base, const TopoDS_Shape& tool)
{
    return differenceResult(base, tool).shape;
}
TopoDS_Shape ShapeFactory::booleanUnion(const TopoDS_Shape& base, const TopoDS_Shape& tool)
{
    return unionResult(base, tool).shape;
}
TopoDS_Shape ShapeFactory::booleanIntersection(const TopoDS_Shape& base, const TopoDS_Shape& tool)
{
    return intersectionResult(base, tool).shape;
}
} // namespace forge::geometry
