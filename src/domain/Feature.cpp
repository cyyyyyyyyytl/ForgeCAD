#include "domain/Feature.h"
#include "geometry/ShapeFactory.h"

namespace forge::domain {
core::ShapeResult Feature::rebuildResult(const std::vector<TopoDS_Shape>& inputs) const
{
    const auto error = validate();
    if (!error.empty()) return {{}, core::RebuildStatus::Failed, error};
    return geometry::ShapeFactory::inspectShape(rebuild(inputs));
}
} // namespace forge::domain
