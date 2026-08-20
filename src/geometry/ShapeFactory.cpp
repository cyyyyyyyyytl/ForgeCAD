#include "geometry/ShapeFactory.h"
#include <spdlog/spdlog.h>
#include <Standard_Failure.hxx>

namespace forge::geometry {

TopoDS_Shape ShapeFactory::makeBox(double length, double width, double height) {
    if (length <= 0 || width <= 0 || height <= 0) {
        spdlog::error("ShapeFactory::makeBox invalid dims: {}x{}x{}", length, width, height);
        return TopoDS_Shape();
    }
    try {
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
        auto s = maker.Shape();
        if (!s.IsNull()) {
            spdlog::info("ShapeFactory::makeBox created {}x{}x{}", length, width, height);
        }
        return s;
    } catch (const Standard_Failure& e) {
        spdlog::error("ShapeFactory::makeBox OCCT exception: {}",
                      e.GetMessageString() ? e.GetMessageString() : "unknown");
        return TopoDS_Shape();
    } catch (...) {
        spdlog::error("ShapeFactory::makeBox unknown exception");
        return TopoDS_Shape();
    }
}

} // namespace forge::geometry
