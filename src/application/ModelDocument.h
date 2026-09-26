#pragma once // ModelDocument 被 UI、AI 和测试共同包含，防止头文件重复展开。

#include "core/ShapeResult.h"
#include "domain/FeatureRegistry.h" // 公开接口使用 NumericParameters 类型。
#include "domain/DependencyGraph.h"
#include <TopoDS_Shape.hxx> // rebuildShapes() 返回按特征 ID 索引的 OCCT 形状。
#include <map>         // 保存“特征类型 -> 已使用序号”的稳定计数器。
#include <memory>      // unique_ptr 表达文档对每个 Feature 的唯一所有权。
#include <string>      // 保存 ID、类型名和快照中的参数名。
#include <string_view> // 查询和修改接口借用字符串，避免无意义复制。
#include <unordered_map> // rebuildShapes() 按稳定 ID 查找本轮计算出的形状。
#include <vector>      // 保存 Feature 顺序以及 Undo/Redo 两个栈。

// 头文件只保存 Feature 的 unique_ptr 和指针；完整定义延迟到 .cpp 包含。
namespace forge::domain {
class Feature;
enum class BooleanOperation;
}

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
    using RebuildReport = std::unordered_map<std::string, core::ShapeResult>;
    RebuildReport rebuildReport() const;
    // 只读收集最终结果；任何重建失败都会拒绝导出，避免把诊断上游当成成品。
    core::ShapeResult shapeForExport() const;
    // 所有容器使用默认构造即可得到合法空文档，因此不需要自定义构造逻辑。
    ModelDocument() = default;
    // 放在 .cpp 定义，因为这里仅前向声明 Feature；销毁 unique_ptr 时才需完整类型。
    ~ModelDocument();

    // 文档具有唯一所有权和独立历史，当前不定义“复制整个文档”的业务语义。
    ModelDocument(const ModelDocument&) = delete;            // 禁止复制构造。
    ModelDocument& operator=(const ModelDocument&) = delete; // 禁止复制赋值。

    // 创建特征：自动分配 Box001 形式的 ID，校验后写入文档并记录 Undo。
    domain::Feature& createFeature( const std::string& type,const domain::NumericParameters& parameters);
    // 已校验的导入几何追加为一个特征，保留已有模型，一次操作可撤销。
    domain::Feature& createImportedFeature(const TopoDS_Shape& shape, const std::string& sourceName);
    // 从两个现有特征创建布尔特征；base 在前、tool 在后，整个创建只占一次 Undo。
    domain::Feature& createBooleanFeature(
        domain::BooleanOperation operation,
        std::string_view baseId,
        std::string_view toolId);
    // 登记现有特征间的依赖关系，并把修改作为一次可撤销操作。
    // 重建几何时会按依赖关系提供上游形状。
    void addDependency(std::string_view featureId, std::string_view dependsOnId);
    // 修改特征的一个参数；对象、参数或范围非法时抛出 invalid_argument。
    void setParameter( std::string_view featureId, std::string_view parameterName,double value);
    // 按稳定 ID 连同所有下游特征删除，并把整个操作记为一次 Undo。
    void deleteFeature(std::string_view featureId);
    // 删除前只读预览：返回依赖者优先、目标最后的全部受影响 ID。
    std::vector<std::string> deletionOrder(std::string_view featureId) const;

    // 历史为空时 undo/redo 安静返回；调用方可先用 canUndo/canRedo 更新按钮状态。
    void undo();
    void redo();
    bool canUndo() const; // 只读检查 Undo 栈是否至少有一张旧快照。
    bool canRedo() const; // 只读检查 Redo 栈是否至少有一张未来快照。

    // 查找返回非拥有指针：Document 仍拥有对象，调用方绝不能 delete。
    // Undo/Redo 会重建 Feature，因此不要长期保存返回指针，应长期保存 ID。
    domain::Feature* findFeature(std::string_view id);
    const domain::Feature* findFeature(std::string_view id) const;
    // 返回只读容器视图：允许 UI 遍历，禁止 UI 绕过 Document 增删元素。
    const std::vector<std::unique_ptr<domain::Feature>>& features() const;
    // 兼容仅需要 Shape 的调用：失败为空 Shape，合法空结果为非 Null 的空 Compound。
    std::unordered_map<std::string, TopoDS_Shape> rebuildShapes() const;
    // 成功及合法空布尔结果隐藏输入；失败时展开到可用上游，按文档顺序返回。
    std::vector<std::string> visibleFeatureIds() const;
    // 使用同一次重建报告计算显示范围，失败结果展开到仍有效的上游。
    std::vector<std::string> visibleFeatureIds(const RebuildReport& report) const;

private:
    // 基本体/布尔快照保存定义；导入特征额外共享不可变的原始 B-Rep，
    // 因为它不能由数值参数重新生成。快照不依赖外部 STEP 文件。
    struct FeatureState {
        std::string id;                     // 恢复后必须保持不变的稳定实例 ID。
        std::string type;                   // Registry 重建具体子类所需的类型名。
        domain::NumericParameters parameters; // 重建该对象所需的全部具名数值。
        std::shared_ptr<const TopoDS_Shape> importedGeometry; // 导入对象无法从数值参数再生成。
        std::string sourceName;
    };
    // 一个 DocumentState 就是一张“整个文档在某一时刻的照片”。
    struct DocumentState {
        std::vector<FeatureState> features;
        domain::DependencyGraph graph;
    };

    // 以下方法只服务于 Document 内部，上层不需要理解即可使用建模 API。
    std::string nextFeatureId(std::string_view type); // 生成 Box001 形式且不重复的 ID。
    DocumentState captureState() const;               // 深拷贝当前轻量模型数据。
    void restoreState(const DocumentState& state);    // 按快照重建 Feature 并恢复依赖图。
    void rememberBeforeChange(DocumentState state);   // 推入 Undo 并清空分叉的 Redo。

    // unique_ptr 表达唯一所有权：Feature 的生命周期由文档统一管理。
    std::vector<std::unique_ptr<domain::Feature>> features_;
    domain::DependencyGraph dependencyGraph_; // 与 features_ 同步的特征 ID 依赖关系。

    // 各类型独立计数，例如 Box 和 Sphere 都可从 001 开始。
    std::map<std::string, int> sequenceByType_;
    // vector 末尾是栈顶；新操作保存“操作前状态”并清空 redoStack_。
    std::vector<DocumentState> undoStack_;
    std::vector<DocumentState> redoStack_;
};

} // namespace forge::application
