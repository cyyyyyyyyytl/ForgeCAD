#pragma once
#include "domain/Feature.h"
#include <memory>

namespace forge::domain {
// 导入几何不是参数化基本体。原始 B-Rep 与文件分离，历史通过不可变共享数据恢复。
class ImportedFeature final : public Feature {
public:
    ImportedFeature(std::string id, std::shared_ptr<const TopoDS_Shape> geometry,
                    std::string sourceName, double x = 0, double y = 0, double z = 0);
    const std::vector<Parameter>& parameters() const override;
    void setParameter(const std::string& name, ParameterValue value) override;
    std::string validate() const override;
    TopoDS_Shape rebuild(const std::vector<TopoDS_Shape>& inputs = {}) const override;
    const std::shared_ptr<const TopoDS_Shape>& geometry() const { return geometry_; }
    const std::string& sourceName() const { return sourceName_; }
private:
    std::shared_ptr<const TopoDS_Shape> geometry_;
    std::string sourceName_;
    std::vector<Parameter> parameters_;
};
} // namespace forge::domain
