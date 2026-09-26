#include "domain/ImportedFeature.h"
#include "domain/PositionParameters.h"
#include "geometry/ShapeFactory.h"
#include <stdexcept>
#include <utility>

namespace forge::domain {
ImportedFeature::ImportedFeature(std::string id, std::shared_ptr<const TopoDS_Shape> geometry,
                                 std::string sourceName, double x, double y, double z)
    : Feature(std::move(id), "Imported"), geometry_(std::move(geometry)), sourceName_(std::move(sourceName)),
      parameters_{{"x", x}, {"y", y}, {"z", z}} {}
const std::vector<Parameter>& ImportedFeature::parameters() const { return parameters_; }
void ImportedFeature::setParameter(const std::string& name, ParameterValue value)
{
    for (auto& parameter : parameters_) {
        if (parameter.name() == name) { parameter.setValue(std::move(value)); return; }
    }
    throw std::invalid_argument("导入特征没有参数: " + name);
}
std::string ImportedFeature::validate() const
{
    if (!geometry_ || geometry_->IsNull()) return "导入特征缺少原始几何";
    return validatePrimitiveParameters(parameters_);
}
TopoDS_Shape ImportedFeature::rebuild(const std::vector<TopoDS_Shape>&) const
{
    if (!validate().empty()) return {};
    // x/y/z 是相对文件原始几何的额外世界坐标平移，默认不改变导入位置。
    return geometry::ShapeFactory::translate(*geometry_, parameters_[0].asDouble(),
        parameters_[1].asDouble(), parameters_[2].asDouble());
}
} // namespace forge::domain
