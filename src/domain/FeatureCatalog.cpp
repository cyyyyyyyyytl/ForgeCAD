#include "domain/FeatureCatalog.h"

#include <algorithm>

namespace forge::domain {

const std::vector<FeatureDescriptor>& FeatureCatalog::all()
{
    static const std::vector<FeatureDescriptor> descriptors = {
        {"Box", {
            {"length", 100.0, 1.0, 10000.0},
            {"width",   50.0, 1.0, 10000.0},
            {"height",  30.0, 1.0, 10000.0},
        }},
        {"Cylinder", {
            {"radius", 20.0, 1.0, 10000.0},
            {"height", 60.0, 1.0, 10000.0},
        }},
        {"Sphere", {
            {"radius", 20.0, 1.0, 10000.0},
        }},
    };
    return descriptors;
}

const FeatureDescriptor* FeatureCatalog::find(std::string_view type)
{
    const auto& descriptors = all();
    const auto it = std::find_if(descriptors.begin(), descriptors.end(),
        [type](const FeatureDescriptor& descriptor) {
            return descriptor.type == type;
        });
    return it == descriptors.end() ? nullptr : &*it;
}

const ParameterDescriptor* FeatureCatalog::findParameter(
    std::string_view type,
    std::string_view parameterName)
{
    const FeatureDescriptor* feature = find(type);
    if (!feature) return nullptr;

    const auto it = std::find_if(feature->parameters.begin(), feature->parameters.end(),
        [parameterName](const ParameterDescriptor& parameter) {
            return parameter.name == parameterName;
        });
    return it == feature->parameters.end() ? nullptr : &*it;
}

} // namespace forge::domain
