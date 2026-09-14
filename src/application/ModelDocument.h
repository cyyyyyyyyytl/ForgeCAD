#pragma once

#include "domain/FeatureRegistry.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace forge::domain { class Feature; }

namespace forge::application {

// ============================================================
// ModelDocument：一个打开的 ForgeCAD 模型文档
// ------------------------------------------------------------
// 对外职责：
//   1. 保存当前文档中的全部 Feature；
//   2. 提供创建、修改、删除、查询这一组稳定 API；
//   3. 提供 Undo/Redo，但隐藏历史记录的实现方式。
//
// 调用方向只有一条：
//   MainWindow / ToolRegistry -> ModelDocument -> FeatureRegistry / Feature
//
// UI 和 AI 不直接修改 features_。以后即使把“快照历史”换成 Command，
// 只要这些公开方法不变，上层代码就不需要跟着重写。
// ============================================================
class ModelDocument {
public:
    ModelDocument() = default;
    // 放在 .cpp 定义，因为这里仅前向声明 Feature；销毁 unique_ptr 时才需完整类型。
    ~ModelDocument();

    // 文档具有唯一所有权和独立历史，当前不定义“复制整个文档”的业务语义。
    ModelDocument(const ModelDocument&) = delete;
    ModelDocument& operator=(const ModelDocument&) = delete;

    // 创建特征：自动分配 Box001 形式的 ID，校验后写入文档并记录 Undo。
    domain::Feature& createFeature(
        const std::string& type,
        const domain::NumericParameters& parameters);
    // 修改特征的一个参数；对象、参数或范围非法时抛出 invalid_argument。
    void setParameter(
        std::string_view featureId,
        std::string_view parameterName,
        double value);
    // 按稳定 ID 删除特征，并把删除前的整个状态加入 Undo 历史。
    void deleteFeature(std::string_view featureId);

    // 历史为空时 undo/redo 安静返回；调用方可先用 canUndo/canRedo 更新按钮状态。
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // 查找返回非拥有指针：Document 仍拥有对象，调用方绝不能 delete。
    // Undo/Redo 会重建 Feature，因此不要长期保存返回指针，应长期保存 ID。
    domain::Feature* findFeature(std::string_view id);
    const domain::Feature* findFeature(std::string_view id) const;
    // 返回只读容器视图：允许 UI 遍历，禁止 UI 绕过 Document 增删元素。
    const std::vector<std::unique_ptr<domain::Feature>>& features() const;

private:
    // FeatureState 只保存可重建模型的数据，不保存 OCCT Shape 或内存地址。
    // Shape 随时能由 Feature::rebuild() 重新计算，没有必要放进历史栈。
    struct FeatureState {
        std::string id;
        std::string type;
        domain::NumericParameters parameters;
    };
    // 一个 DocumentState 就是一张“整个文档在某一时刻的照片”。
    using DocumentState = std::vector<FeatureState>;

    // 以下方法只服务于 Document 内部，上层不需要理解即可使用建模 API。
    std::string nextFeatureId(std::string_view type);
    DocumentState captureState() const;
    void restoreState(const DocumentState& state);
    void rememberBeforeChange(DocumentState state);

    // unique_ptr 表达唯一所有权：Feature 的生命周期由文档统一管理。
    std::vector<std::unique_ptr<domain::Feature>> features_;
    // 各类型独立计数，例如 Box 和 Sphere 都可从 001 开始。
    std::map<std::string, int> sequenceByType_;
    // vector 末尾是栈顶；新操作保存“操作前状态”并清空 redoStack_。
    std::vector<DocumentState> undoStack_;
    std::vector<DocumentState> redoStack_;
};

} // namespace forge::application
