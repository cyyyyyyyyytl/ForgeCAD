#pragma once
#include "domain/ExtrudeFeature.h"

namespace forge::domain {

// 两个固定输入依次为主体、草图；工具实体只在重建时生成。
class ExtrudeCutFeature final : public Feature {
public:
    ExtrudeCutFeature(std::string id, double height,
        ExtrudeDirection direction = ExtrudeDirection::Forward);
    const std::vector<Parameter>& parameters() const override;
    void setParameter(const std::string& name, ParameterValue value) override;
    std::string validate() const override;
    TopoDS_Shape rebuild(const std::vector<TopoDS_Shape>& inputs = {}) const override;
    core::ShapeResult rebuildResult(const std::vector<TopoDS_Shape>& inputs = {}) const override;
private:
    ExtrudeFeature extrusion_;
};
}
