#include "application/ModelingService.h"

#include "application/CreateFeatureCommand.h"
#include "application/ModelDocument.h"
#include "application/ModifyParameterCommand.h"
#include "domain/Feature.h"
#include "domain/FeatureFactory.h"

#include <memory>
#include <stdexcept>

namespace forge::application {

ModelingService::ModelingService(ModelDocument& document)
    // 服务不拥有文档；MainWindow 负责保证 document_ 比本服务活得更久。
    : document_(document)
{
}

domain::Feature& ModelingService::createFeature(
    const std::string& type,
    const domain::NumericParameters& parameters)
{
    // “生成 ID -> 创建对象 -> 领域校验 -> 写入文档”构成一次完整的创建事务；
    // 任一步抛出异常时，尚未 addFeature，因此文档不会留下半成品。
    // ID 属于文档级身份管理，不让 UI 或 AI 自己编造，避免重复。
    const std::string id = document_.nextFeatureId(type);
    // AI 返回的是具名参数；Factory 会根据 Catalog 转为具体构造函数需要的顺序。
    auto feature = domain::FeatureFactory::createNamed(type, id, parameters);
    // Catalog 是入口 Schema，Feature::validate() 是领域对象最后一道防线。
    const std::string validationError = feature->validate();
    if (!validationError.empty()) {
        throw std::invalid_argument(validationError);
    }
    // 保存 ID 后把 Feature 交给创建命令；命令执行成功后对象由 Document 持有。
    // 再按稳定 ID 找回引用，调用方无需了解命令对象的所有权流转。
    commandManager_.executeCommand(
        std::make_unique<CreateFeatureCommand>(document_, std::move(feature)));
    return *document_.findFeature(id);
}

void ModelingService::setParameter(
    std::string_view featureId,
    std::string_view parameterName,
    double value)
{
    // Service 先按稳定 ID 找到真正需要修改的 Feature。
    domain::Feature* feature = document_.findFeature(featureId);
    if (!feature) {
        throw std::invalid_argument(
            "找不到 Feature: " + std::string(featureId));
    }

    // 创建一张“修改参数命令卡”，再交给 CommandManager 执行和保存。
    commandManager_.executeCommand(
        std::make_unique<ModifyParameterCommand>(
            *feature,
            std::string(parameterName),
            value));
}

void ModelingService::undo()
{
    commandManager_.undo();
}

void ModelingService::redo()
{
    commandManager_.redo();
}

bool ModelingService::canUndo() const
{
    return commandManager_.canUndo();
}

bool ModelingService::canRedo() const
{
    return commandManager_.canRedo();
}

} // namespace forge::application
