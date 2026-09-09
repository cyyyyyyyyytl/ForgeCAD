#include "application/ModelDocument.h"

#include "domain/Feature.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace forge::application {

ModelDocument::~ModelDocument() = default;

domain::Feature& ModelDocument::addFeature(std::unique_ptr<domain::Feature> feature)
{
    if (!feature) {
        throw std::invalid_argument("不能向文档加入空 Feature");
    }
    if (findFeature(feature->id())) {
        throw std::invalid_argument("Feature ID 已存在: " + feature->id());
    }

    features_.push_back(std::move(feature));
    return *features_.back();
}

domain::Feature* ModelDocument::findFeature(std::string_view id)
{
    const auto it = std::find_if(features_.begin(), features_.end(),
        [id](const auto& feature) { return feature->id() == id; });
    return it == features_.end() ? nullptr : it->get();
}

const domain::Feature* ModelDocument::findFeature(std::string_view id) const
{
    const auto it = std::find_if(features_.begin(), features_.end(),
        [id](const auto& feature) { return feature->id() == id; });
    return it == features_.end() ? nullptr : it->get();
}

const std::vector<std::unique_ptr<domain::Feature>>& ModelDocument::features() const
{
    return features_;
}

std::string ModelDocument::nextFeatureId(std::string_view type)
{
    const std::string typeName(type);
    auto& sequence = sequenceByType_[typeName];
    std::string candidate;
    do {
        std::ostringstream stream;
        stream << typeName << std::setw(3) << std::setfill('0') << ++sequence;
        candidate = stream.str();
    } while (findFeature(candidate));
    return candidate;
}

} // namespace forge::application
