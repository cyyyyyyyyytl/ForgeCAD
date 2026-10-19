#include "application/ModelCommands.h"

#include "application/ModelDocument.h" // 私有原语定义在文档内部，命令通过 friend 访问。
#include "domain/Feature.h"            // 构造命令时需要读取 Feature::id()。

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace forge::application {

// ============================================================
// CreateFeatureCommand
// ============================================================
CreateFeatureCommand::CreateFeatureCommand(std::unique_ptr<domain::Feature> feature,
    std::vector<std::string> dependencies, std::size_t index)
    : feature_(std::move(feature))
    , dependencies_(std::move(dependencies))
    , index_(index)
{
    if (!feature_) {
        throw std::invalid_argument("创建命令缺少特征对象");
    }
    // ID 在创建时固定下来：即使对象暂时被命令持有，重做也必须插回原 ID。
    id_ = feature_->id();
}

void CreateFeatureCommand::execute(ModelDocument& document)
{
    if (!feature_) {
        // execute 只能从“命令持有对象”的状态调用；否则说明历史栈状态已损坏。
        throw std::logic_error("创建命令不能重复执行: " + id_);
    }
    // 先插入对象（及图节点），再按原顺序恢复依赖边。
    // 重做与首次执行走同一条路径，保证文档顺序和依赖顺序完全一致。
    document.insertFeatureRaw(std::move(feature_), index_);
    for (const auto& dependency : dependencies_) {
        document.addDependencyRaw(id_, dependency);
    }
}

void CreateFeatureCommand::undo(ModelDocument& document)
{
    std::size_t index = index_;
    feature_ = document.takeFeatureRaw(id_, index); // 对象重新归命令所有，等待重做。
}

std::string CreateFeatureCommand::label() const
{
    return "创建 " + id_;
}

// ============================================================
// ModifyParameterCommand
// ============================================================
ModifyParameterCommand::ModifyParameterCommand(std::string featureId,
    std::string parameterName, double oldValue, double newValue)
    : featureId_(std::move(featureId))
    , parameterName_(std::move(parameterName))
    , oldValue_(oldValue)
    , newValue_(newValue)
{
}

void ModifyParameterCommand::execute(ModelDocument& document)
{
    document.setFeatureParameterRaw(featureId_, parameterName_, newValue_);
}

void ModifyParameterCommand::undo(ModelDocument& document)
{
    document.setFeatureParameterRaw(featureId_, parameterName_, oldValue_);
}

std::string ModifyParameterCommand::label() const
{
    return "修改 " + featureId_ + "." + parameterName_;
}

// ============================================================
// SetDefinitionCommand
// ============================================================
SetDefinitionCommand::SetDefinitionCommand(std::string featureId,
    domain::FeatureDefinition oldDefinition, domain::FeatureDefinition newDefinition)
    : featureId_(std::move(featureId))
    , oldDefinition_(std::move(oldDefinition))
    , newDefinition_(std::move(newDefinition))
{
}

void SetDefinitionCommand::execute(ModelDocument& document)
{
    document.setFeatureDefinitionRaw(featureId_, newDefinition_);
}

void SetDefinitionCommand::undo(ModelDocument& document)
{
    document.setFeatureDefinitionRaw(featureId_, oldDefinition_);
}

std::string SetDefinitionCommand::label() const
{
    return "修改定义 " + featureId_;
}

// ============================================================
// DeleteFeatureCommand
// ============================================================
DeleteFeatureCommand::DeleteFeatureCommand(ModelDocument& document, std::string featureId)
    : rootId_(std::move(featureId))
{
    // 构造阶段只读取删除范围、原下标和依赖，不改动文档；
    // 真正把对象移出文档发生在 execute()，这样“记录历史”和“执行删除”保持同一步。
    for (const auto& id : document.deletionOrder(rootId_)) {
        RemovedFeature item;
        item.id = id;
        item.index = document.indexOfFeature(id);
        item.dependencies = document.dependenciesOf(id);
        removed_.push_back(std::move(item));
    }
}

void DeleteFeatureCommand::execute(ModelDocument& document)
{
    // deletionOrder 保证下游在前、目标在后，每一步删除时都不会留下指向
    // 已删除对象的依赖边，因此 takeFeatureRaw 的防御性检查可以满足。
    for (auto& item : removed_) {
        std::size_t index = item.index;
        item.feature = document.takeFeatureRaw(item.id, index);
    }
}

void DeleteFeatureCommand::undo(ModelDocument& document)
{
    // 恢复顺序不能直接用删除顺序：addDependency 允许“先创建 A、后创建 B，
    // 再让 A 依赖 B”，此时依赖方的文档下标可能更小。先按原下标升序插回
    // 所有对象（只建节点），再统一恢复依赖边，任何组合都能正确还原。
    std::vector<std::size_t> order(removed_.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [this](std::size_t left, std::size_t right) {
        return removed_[left].index < removed_[right].index;
    });
    for (const std::size_t position : order) {
        auto& item = removed_[position];
        if (!item.feature) {
            throw std::logic_error("删除命令撤销时缺少特征对象: " + item.id);
        }
        document.insertFeatureRaw(std::move(item.feature), item.index);
    }
    for (auto& item : removed_) {
        for (const auto& dependency : item.dependencies) {
            document.addDependencyRaw(item.id, dependency);
        }
    }
}

std::string DeleteFeatureCommand::label() const
{
    return "删除 " + rootId_ + (removed_.size() > 1
        ? "（级联 " + std::to_string(removed_.size()) + " 个）"
        : std::string());
}

// ============================================================
// AddDependencyCommand
// ============================================================
AddDependencyCommand::AddDependencyCommand(std::string featureId, std::string dependsOnId)
    : featureId_(std::move(featureId))
    , dependsOnId_(std::move(dependsOnId))
{
}

void AddDependencyCommand::execute(ModelDocument& document)
{
    document.addDependencyRaw(featureId_, dependsOnId_);
}

void AddDependencyCommand::undo(ModelDocument& document)
{
    if (!document.removeDependencyRaw(featureId_, dependsOnId_)) {
        throw std::logic_error("撤销依赖登记失败: " + featureId_ + " -> " + dependsOnId_);
    }
}

std::string AddDependencyCommand::label() const
{
    return "登记依赖 " + featureId_ + " -> " + dependsOnId_;
}

// ============================================================
// CompositeCommand
// ============================================================
void CompositeCommand::add(std::unique_ptr<Command> command)
{
    if (!command) {
        throw std::invalid_argument("组合命令不能包含空命令");
    }
    commands_.push_back(std::move(command));
}

void CompositeCommand::execute(ModelDocument& document)
{
    // 子命令按添加顺序执行；任何一步失败都会把异常交给 CommandManager，
    // 由 ModelDocument 的调用方保证不压栈（复合命令的原子性依赖前置校验）。
    for (auto& command : commands_) {
        command->execute(document);
    }
}

void CompositeCommand::undo(ModelDocument& document)
{
    // 撤销必须逆序，才能像栈一样精确回到组合执行前的状态。
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
        (*it)->undo(document);
    }
}

std::string CompositeCommand::label() const
{
    if (commands_.empty()) {
        return "空组合命令";
    }
    std::string result = commands_.front()->label();
    if (commands_.size() > 1) {
        result += " 等 " + std::to_string(commands_.size()) + " 项";
    }
    return result;
}

} // namespace forge::application
