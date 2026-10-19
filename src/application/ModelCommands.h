#pragma once // 具体命令只被 ModelDocument 使用，但仍独立成文件便于阅读和测试。

#include "application/Command.h"
#include "domain/FeatureDefinition.h" // SetDefinitionCommand 保存结构化定义的新旧值。

#include <cstddef> // std::size_t：文档中特征顺序下标。
#include <memory>  // unique_ptr：命令在“撤销状态”下拥有被移出文档的 Feature。
#include <string>
#include <vector>

namespace forge::domain {
class Feature; // 命令只在 .cpp 里调用 Feature 接口，头文件前向声明即可。
}

namespace forge::application {

class ModelDocument;

// ============================================================
// 具体命令：每个类只负责一种修改，并保存撤销所需的最小数据
// ------------------------------------------------------------
// ModelDocument 是唯一允许修改 features_/dependencyGraph_ 的地方；
// 这些命令通过 friend 访问它的私有原语（insertFeature/takeFeature 等），
// 而不是调用公开 API，避免“执行命令本身又生成一条历史”的递归。
// ============================================================

// 创建特征。命令持有尚未进入文档的 Feature；撤销时把对象取回命令，
// 重做时再交还文档。因此 Undo/Redo 后 ID、参数和对象地址都不变。
class CreateFeatureCommand final : public Command {
public:
    CreateFeatureCommand(std::unique_ptr<domain::Feature> feature,
                         std::vector<std::string> dependencies,
                         std::size_t index);
    void execute(ModelDocument& document) override;
    void undo(ModelDocument& document) override;
    std::string label() const override;

private:
    std::unique_ptr<domain::Feature> feature_; // execute 前/Undo 后由命令拥有。
    std::string id_;                           // 稳定 ID，不依赖 Feature 指针。
    std::vector<std::string> dependencies_;    // 重做时按原顺序恢复依赖边。
    std::size_t index_ = 0;                    // 原文档顺序，重做时插回同一位置。
};

// 修改一个数值参数：只记录参数名和旧值/新值，不复制整份参数表。
class ModifyParameterCommand final : public Command {
public:
    ModifyParameterCommand(std::string featureId, std::string parameterName,
                           double oldValue, double newValue);
    void execute(ModelDocument& document) override;
    void undo(ModelDocument& document) override;
    std::string label() const override;

private:
    std::string featureId_;
    std::string parameterName_;
    double oldValue_ = 0.0;
    double newValue_ = 0.0;
};

// 修改结构化定义（轮廓顶点、空间路径、选边 ID）。定义通常比整个文档小得多。
class SetDefinitionCommand final : public Command {
public:
    SetDefinitionCommand(std::string featureId, domain::FeatureDefinition oldDefinition,
                         domain::FeatureDefinition newDefinition);
    void execute(ModelDocument& document) override;
    void undo(ModelDocument& document) override;
    std::string label() const override;

private:
    std::string featureId_;
    domain::FeatureDefinition oldDefinition_;
    domain::FeatureDefinition newDefinition_;
};

// 级联删除：一次删除目标特征和它的全部下游。
// execute 时按“下游先、目标后”的顺序把对象移入命令；undo 时按原文档下标
// 升序恢复对象，再按原顺序恢复依赖边，保证文档顺序与依赖图都回到删除前。
class DeleteFeatureCommand final : public Command {
public:
    DeleteFeatureCommand(ModelDocument& document, std::string featureId);
    void execute(ModelDocument& document) override;
    void undo(ModelDocument& document) override;
    std::string label() const override;

private:
    struct RemovedFeature {
        std::string id;
        std::size_t index = 0;
        std::vector<std::string> dependencies;
        std::unique_ptr<domain::Feature> feature; // execute 后由命令持有。
    };

    std::string rootId_;             // 用户真正要删除的目标。
    std::vector<RemovedFeature> removed_; // 删除顺序：下游在前、目标在后。
};

// 登记一条特征依赖关系。撤销删除这条边，重做重新加回。
class AddDependencyCommand final : public Command {
public:
    AddDependencyCommand(std::string featureId, std::string dependsOnId);
    void execute(ModelDocument& document) override;
    void undo(ModelDocument& document) override;
    std::string label() const override;

private:
    std::string featureId_;
    std::string dependsOnId_;
};

// 组合命令：把多条子命令表达成“一次用户操作”，整组只占一条历史。
// 典型用途：一次设置 XYZ（三个 ModifyParameterCommand）、
// 一次定义编辑（多个参数修改 + 一个 SetDefinitionCommand）。
class CompositeCommand final : public Command {
public:
    // 按顺序追加子命令；execute 顺序执行，undo 逆序撤销。
    void add(std::unique_ptr<Command> command);
    bool empty() const { return commands_.empty(); }
    std::string label() const override;

    void execute(ModelDocument& document) override;
    void undo(ModelDocument& document) override;

private:
    std::vector<std::unique_ptr<Command>> commands_;
};

} // namespace forge::application
