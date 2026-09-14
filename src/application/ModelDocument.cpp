#include "application/ModelDocument.h"

#include "domain/Feature.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace forge::application {

ModelDocument::~ModelDocument() = default;

// 创建流程：分配 ID -> Registry 创建对象 -> 领域校验 -> 保存旧状态 -> 加入文档。
domain::Feature& ModelDocument::createFeature(
    const std::string& type,
    const domain::NumericParameters& parameters)
{
    // ID 由文档统一产生，避免 UI 与 AI 各自编号后发生冲突。
    const std::string id = nextFeatureId(type);
    // Registry 将字符串类型和具名参数转换成具体的 Box/Cylinder/Sphere 对象。
    auto feature = domain::FeatureRegistry::create(type, id, parameters);

    // Registry 检查外部参数格式；Feature::validate() 再保护对象自身不变量。
    const std::string validationError = feature->validate();
    if (!validationError.empty()) {
        throw std::invalid_argument(validationError);
    }

    // 只有所有校验通过后才拍快照，失败的创建不会污染 Undo 历史。
    DocumentState before = captureState();
    // 所有权在这里从局部 unique_ptr 转移到 features_。
    features_.push_back(std::move(feature));
    rememberBeforeChange(std::move(before));
    // vector 保存的是 unique_ptr；返回的是它所指向 Feature 的非拥有引用。
    return *features_.back();
}

// 参数修改流程：定位对象 -> 查参数规则 -> 保存旧状态 -> 修改 -> 领域校验。
void ModelDocument::setParameter(
    std::string_view featureId,
    std::string_view parameterName,
    double value)
{
    // 稳定 ID 是 UI、AI、历史恢复共同使用的对象身份。
    domain::Feature* feature = findFeature(featureId);
    if (!feature) {
        throw std::invalid_argument("找不到 Feature: " + std::string(featureId));
    }

    // Registry 是参数名、最小值、最大值的唯一说明来源。
    const domain::ParameterDescriptor* descriptor =
        domain::FeatureRegistry::findParameter(feature->type(), parameterName);
    if (!descriptor) {
        throw std::invalid_argument(
            feature->type() + " 没有参数: " + std::string(parameterName));
    }
    if (value < descriptor->minimum || value > descriptor->maximum) {
        throw std::invalid_argument(
            feature->type() + " 参数超出范围: " + std::string(parameterName));
    }

    // 先保存旧状态，后续领域校验意外失败时可以完整回滚。
    DocumentState before = captureState();
    feature->setParameter(std::string(parameterName), value);
    const std::string validationError = feature->validate();
    if (!validationError.empty()) {
        restoreState(before); // 保证失败操作对用户来说“什么都没发生”。
        throw std::invalid_argument(validationError);
    }

    // 修改成功后旧状态成为一次可撤销记录，并废弃旧的 Redo 分支。
    rememberBeforeChange(std::move(before));
}

// 删除不把 Feature 临时交给其他对象；快照里已经保存了恢复它所需的全部数据。
void ModelDocument::deleteFeature(std::string_view featureId)
{
    const auto it = std::find_if(
        features_.begin(), features_.end(),
        [featureId](const auto& feature) {
            return feature->id() == featureId;
        });
    if (it == features_.end()) {
        throw std::invalid_argument(
            "找不到要删除的 Feature: " + std::string(featureId));
    }

    // 先保存包含待删除对象的状态，再由 unique_ptr 正常销毁该对象。
    DocumentState before = captureState();
    features_.erase(it);
    rememberBeforeChange(std::move(before));
}

void ModelDocument::undo()
{
    if (undoStack_.empty()) {
        return;
    }

    // 当前状态稍后可用于 Redo；Undo 栈顶则是需要恢复的上一个状态。
    DocumentState current = captureState();
    DocumentState previous = std::move(undoStack_.back());
    undoStack_.pop_back();
    restoreState(previous);
    redoStack_.push_back(std::move(current));
}

void ModelDocument::redo()
{
    if (redoStack_.empty()) {
        return;
    }

    // Redo 与 Undo 对称：保存当前状态，再恢复 Redo 栈顶。
    DocumentState current = captureState();
    DocumentState next = std::move(redoStack_.back());
    redoStack_.pop_back();
    restoreState(next);
    undoStack_.push_back(std::move(current));
}

bool ModelDocument::canUndo() const
{
    return !undoStack_.empty();
}

bool ModelDocument::canRedo() const
{
    return !redoStack_.empty();
}

domain::Feature* ModelDocument::findFeature(std::string_view id)
{
    // 当前 MVP 特征数量少，线性查找最直观；性能需要时可加 ID 索引而不改 API。
    const auto it = std::find_if(
        features_.begin(), features_.end(),
        [id](const auto& feature) { return feature->id() == id; });
    return it == features_.end() ? nullptr : it->get();
}

const domain::Feature* ModelDocument::findFeature(std::string_view id) const
{
    const auto it = std::find_if(
        features_.begin(), features_.end(),
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
    // map 的 operator[] 会在类型首次出现时建立值为 0 的计数器。
    auto& sequence = sequenceByType_[typeName];
    std::string candidate;
    do {
        std::ostringstream stream;
        stream << typeName << std::setw(3) << std::setfill('0') << ++sequence;
        candidate = stream.str();
    } while (findFeature(candidate)); // 兼容未来导入带现成 ID 的文档。
    return candidate;
}

ModelDocument::DocumentState ModelDocument::captureState() const
{
    // 深拷贝的是轻量字符串和数值，不复制 unique_ptr，也不计算 OCCT Shape。
    DocumentState state;
    state.reserve(features_.size());
    for (const auto& feature : features_) {
        FeatureState saved{feature->id(), feature->type(), {}};
        for (const auto& parameter : feature->parameters()) {
            saved.parameters.emplace(parameter.name(), parameter.asDouble());
        }
        state.push_back(std::move(saved));
    }
    return state;
}

void ModelDocument::restoreState(const DocumentState& state)
{
    // 先在临时 vector 中完整重建；全部成功后才替换当前文档。
    // 因而创建中途抛异常时，原 features_ 仍保持不变。
    std::vector<std::unique_ptr<domain::Feature>> restored;
    restored.reserve(state.size());
    for (const auto& saved : state) {
        restored.push_back(domain::FeatureRegistry::create(
            saved.type, saved.id, saved.parameters));
    }
    features_ = std::move(restored);
}

void ModelDocument::rememberBeforeChange(DocumentState state)
{
    // 用户在 Undo 后做出新修改时，旧 Redo 分支已经不再属于当前时间线。
    undoStack_.push_back(std::move(state));
    redoStack_.clear();
}

} // namespace forge::application
