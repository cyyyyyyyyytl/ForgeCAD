#include "application/ModelingService.h"

#include "application/ModelDocument.h"
#include "domain/Feature.h"
#include "domain/FeatureFactory.h"

#include <stdexcept>

namespace forge::application {

ModelingService::ModelingService(ModelDocument& document)
    : document_(document)
{
}

domain::Feature& ModelingService::createFeature(
    const std::string& type,
    const domain::NumericParameters& parameters)
{
    // ID 属于文档级身份管理，不让 UI 或 AI 自己编造，避免重复。
    const std::string id = document_.nextFeatureId(type);
    // AI 返回的是具名参数；Factory 会根据 Catalog 转为具体构造函数需要的顺序。
    auto feature = domain::FeatureFactory::createNamed(type, id, parameters);
    // Catalog 是入口 Schema，Feature::validate() 是领域对象最后一道防线。
    const std::string validationError = feature->validate();
    if (!validationError.empty()) {
        throw std::invalid_argument(validationError);
    }
    return document_.addFeature(std::move(feature));
}

void ModelingService::setParameter(
    std::string_view featureId,
    std::string_view parameterName,
    double value)
{
    // 外部入口只传稳定 ID，不保存 vector 下标或裸指针。
    domain::Feature* feature = document_.findFeature(featureId);
    if (!feature) {
        throw std::invalid_argument("找不到 Feature: " + std::string(featureId));
    }

    const domain::ParameterDescriptor* descriptor =
        domain::FeatureCatalog::findParameter(feature->name(), parameterName);
    if (!descriptor) {
        throw std::invalid_argument(feature->name() + " 没有参数: "
                                    + std::string(parameterName));
    }
    if (value < descriptor->minimum || value > descriptor->maximum) {
        throw std::invalid_argument("参数 " + std::string(parameterName) + " 超出允许范围");
    }

    // 修改前保存旧值：当前虽未实现完整 Undo，但至少保证失败不会污染文档状态。
    double oldValue = 0.0;
    bool found = false;
    for (const auto& parameter : feature->parameters()) {
        if (parameter.name() == parameterName) {
            oldValue = parameter.asDouble();
            found = true;
            break;
        }
    }
    if (!found) {
        throw std::invalid_argument(feature->name() + " 没有参数: "
                                    + std::string(parameterName));
    }

    feature->setParameter(std::string(parameterName), value);
    const std::string validationError = feature->validate();
    if (!validationError.empty()) {
        // 事务式“小回滚”：领域校验失败，恢复到调用前的合法值。
        feature->setParameter(std::string(parameterName), oldValue);
        throw std::invalid_argument(validationError);
    }
}

} // namespace forge::application
