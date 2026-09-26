#pragma once
#include <TopoDS_Shape.hxx>
#include <string>

namespace forge::core {
// 重建状态是由参数推导的临时数据，不进入 Undo 快照或原生文件。
enum class RebuildStatus { Ready, Empty, Failed, Blocked };
struct ShapeResult {
    TopoDS_Shape shape;
    RebuildStatus status = RebuildStatus::Failed;
    std::string message;
    bool usable() const { return status == RebuildStatus::Ready || status == RebuildStatus::Empty; }
};
} // namespace forge::core
