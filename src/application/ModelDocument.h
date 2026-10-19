#pragma once // ModelDocument 被 UI、AI 和测试共同包含，防止头文件重复展开。
#include "application/CommandManager.h" // 文档内部持有 Undo/Redo 命令双栈。
#include "application/DocumentData.h"
#include <cstdint>
#include "core/ShapeResult.h"
#include "domain/FeatureDefinition.h" // 命令原语需要按值传递结构化定义。
#include "domain/FeatureRegistry.h" // 公开接口使用 NumericParameters 类型。
#include "domain/DependencyGraph.h"
#include "domain/ExtrudeFeature.h"
#include "domain/RevolveFeature.h"
#include <TopoDS_Shape.hxx> // rebuildShapes() 返回按特征 ID 索引的 OCCT 形状。
#include <map>         // 保存“特征类型 -> 已使用序号”的稳定计数器。
#include <memory>      // unique_ptr 表达文档对每个 Feature 的唯一所有权。
#include <string>      // 保存 ID、类型名和历史命令中的参数名。
#include <string_view> // 查询和修改接口借用字符串，避免无意义复制。
#include <unordered_map> // rebuildShapes() 按稳定 ID 查找本轮计算出的形状。
#include <vector>      // 保存 Feature 顺序、历史命令栈和快照数据。

// 头文件只保存 Feature 的 unique_ptr 和指针；完整定义延迟到 .cpp 包含。
namespace forge::domain {
class Feature;
enum class BooleanOperation;
}

namespace forge::application {

// 具体命令需要在文档内部执行增量修改；实现放在 ModelCommands.cpp。
class CreateFeatureCommand;
class ModifyParameterCommand;
class SetDefinitionCommand;
class DeleteFeatureCommand;
class AddDependencyCommand;

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
// UI 和 AI 不直接修改 features_。Undo/Redo 由内部 CommandManager 的命令栈
// 实现，公开方法保持不变，因此上层代码不感知历史实现方式。
// ============================================================
class ModelDocument {
public:
    using RebuildReport = std::unordered_map<std::string, core::ShapeResult>;
    RebuildReport rebuildReport() const;
    // 只读收集最终结果；任何重建失败都会拒绝导出，避免把诊断上游当成成品。
    core::ShapeResult shapeForExport() const;
    // 所有容器使用默认构造即可得到合法空文档，因此不需要自定义构造逻辑。
    ModelDocument();
    // 放在 .cpp 定义，因为这里仅前向声明 Feature；销毁 unique_ptr 时才需完整类型。
    ~ModelDocument();

    // 文档具有唯一所有权和独立历史，当前不定义“复制整个文档”的业务语义。
    ModelDocument(const ModelDocument&) = delete;            // 禁止复制构造。
    ModelDocument& operator=(const ModelDocument&) = delete; // 禁止复制赋值。

    // 创建特征：自动分配 Box001 形式的 ID，校验后写入文档并记录 Undo。
    domain::Feature& createFeature( const std::string& type,const domain::NumericParameters& parameters);
    domain::Feature& createAdvancedFeature(const std::string& type,const domain::NumericParameters& parameters,
        domain::FeatureDefinition definition={},const std::vector<std::string>& inputs={});
    void setAdvancedDefinition(std::string_view id,const domain::NumericParameters& parameters,domain::FeatureDefinition definition);
    // 已校验的导入几何追加为一个特征，保留已有模型，一次操作可撤销。
    domain::Feature& createImportedFeature(const TopoDS_Shape& shape, const std::string& sourceName);
    // 一个草图输入、正高度与方向；创建和依赖登记只占一次 Undo。
    domain::Feature& createExtrudeFeature(std::string_view sketchId, double height,
        domain::ExtrudeDirection direction = domain::ExtrudeDirection::Forward);
    domain::Feature& createExtrudeCutFeature(std::string_view baseId, std::string_view sketchId,
        double height, domain::ExtrudeDirection direction = domain::ExtrudeDirection::Forward);
    domain::Feature& createRevolveFeature(std::string_view sketchId, double angle,
        domain::RevolveAxis axis = domain::RevolveAxis::Y);
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
    // 同时设置 XYZ，一次点击定位只占一条历史。
    void setPosition(std::string_view featureId, double x, double y, double z);
    // 按稳定 ID 连同所有下游特征删除，并把整个操作记为一次 Undo。
    void deleteFeature(std::string_view featureId);
    // 删除前只读预览：返回依赖者优先、目标最后的全部受影响 ID。
    std::vector<std::string> deletionOrder(std::string_view featureId) const;
    // 保留输入顺序；供 AI 查询依赖，无需序列化整个文档及 BRep 资产。
    std::vector<std::string> dependenciesOf(std::string_view featureId) const;

    // 历史为空时 undo/redo 安静返回；调用方可先用 canUndo/canRedo 更新按钮状态。
    void undo();
    void redo();
    bool canUndo() const; // 只读检查命令栈是否至少有一条可撤销记录。
    bool canRedo() const; // 只读检查命令栈是否至少有一条可重做记录。

    // 查找返回非拥有指针：Document 仍拥有对象，调用方绝不能 delete。
    // Undo/Redo 现在按命令增量回放，不再整体重建 Feature；但“删除/撤销创建”
    // 期间对象归命令所有，指针仍可能失效，跨历史长期保存的应始终是稳定 ID。
    // 公开接口只暴露只读视图：任何参数修改都必须经过 Document 的范围校验和历史记录。
    const domain::Feature* findFeature(std::string_view id) const;
    // 返回只读容器视图：允许 UI 遍历，禁止 UI 绕过 Document 增删元素。
    const std::vector<std::unique_ptr<domain::Feature>>& features() const;
    // 兼容仅需要 Shape 的调用：失败为空 Shape，合法空结果为非 Null 的空 Compound。
    std::unordered_map<std::string, TopoDS_Shape> rebuildShapes() const;
    // 成功及合法空布尔结果隐藏输入；失败时展开到可用上游，按文档顺序返回。
    std::vector<std::string> visibleFeatureIds() const;
    // 使用同一次重建报告计算显示范围，失败结果展开到仍有效的上游。
    std::vector<std::string> visibleFeatureIds(const RebuildReport& report) const;
    // 只读导出建模数据，不修改模型或 Undo/Redo。
    DocumentData exportData() const;
    // 完整校验并准备临时对象，成功后替换；打开文档清空会话历史。
    void replaceData(const DocumentData& data);
    bool isModified() const { return revision_ != savedRevision_; }
    void markSaved() { savedRevision_ = revision_; }
    void swap(ModelDocument& other) noexcept;

private:
    // 打开文档时的“整份数据”结构。现在只用于 replaceData() 的事务式替换，
    // 不再作为 Undo/Redo 历史保存：历史由 CommandManager 的命令栈承载。
    struct FeatureState {
        std::string id;                     // 恢复后必须保持不变的稳定实例 ID。
        std::string type;                   // Registry 重建具体子类所需的类型名。
        domain::NumericParameters parameters; // 重建该对象所需的全部具名数值。
        std::shared_ptr<const TopoDS_Shape> importedGeometry; // 导入对象无法从数值参数再生成。
        std::string sourceName;
        domain::FeatureDefinition definition;
    };
    // 一个 DocumentState 是一份待装载文档的完整数据（不是历史快照）。
    struct DocumentState {
        std::uint64_t revision = 0;
        std::vector<FeatureState> features;
        domain::DependencyGraph graph;
    };

    // 以下方法只服务于 Document 内部，上层不需要理解即可使用建模 API。
    // 内部写路径专用：返回可修改指针，避免为了一个 setParameter 把 mutable 重载暴露给 UI/AI。
    domain::Feature* findFeatureForWrite(std::string_view id);
    std::string nextFeatureId(std::string_view type); // 生成 Box001 形式且不重复的 ID。
    void restoreState(const DocumentState& state);    // 仅用于打开文档：整体替换 Feature 和依赖图。

    // ---- 命令模式内部原语 ----------------------------------------------------
    // 这些方法不记录历史、不做 UI 级校验，只做“一步到位”的增量修改；
    // 由 ModelCommands.cpp 里的具体命令在 execute/undo 中调用。
    // 这样公开 API 负责校验 + 生成命令 + 压栈，命令负责精确回放，职责不混淆。
    void runCommand(std::unique_ptr<Command> command); // 执行命令并记录到历史。
    // 插入对象（不含依赖边），index 为原文档顺序；重复 ID 会抛 logic_error。
    void insertFeatureRaw(std::unique_ptr<domain::Feature> feature, std::size_t index);
    // 按 ID 移出对象并交还给命令；仍被下游依赖时抛 logic_error。
    std::unique_ptr<domain::Feature> takeFeatureRaw(std::string_view id, std::size_t& index);
    std::size_t indexOfFeature(std::string_view id) const;
    void setFeatureParameterRaw(std::string_view id, std::string_view parameterName, double value);
    void setFeatureDefinitionRaw(std::string_view id, domain::FeatureDefinition definition);
    void addDependencyRaw(std::string_view featureId, std::string_view dependsOnId);
    bool removeDependencyRaw(std::string_view featureId, std::string_view dependsOnId);

    // 命令需要访问上述私有原语；friends 只用于命令实现，不扩大公开 API。
    friend class CreateFeatureCommand;
    friend class ModifyParameterCommand;
    friend class SetDefinitionCommand;
    friend class DeleteFeatureCommand;
    friend class AddDependencyCommand;

    // unique_ptr 表达唯一所有权：Feature 的生命周期由文档统一管理。
    std::vector<std::unique_ptr<domain::Feature>> features_;
    domain::DependencyGraph dependencyGraph_; // 与 features_ 同步的特征 ID 依赖关系。

    // 各类型独立计数，例如 Box 和 Sphere 都可从 001 开始。
    std::map<std::string, int> sequenceByType_;
    // Undo/Redo 由命令栈管理：每条记录只保存增量数据，不再保存整份文档。
    CommandManager history_;
    std::string documentId_; // 文档身份，不随编辑和撤销重做改变。
    std::uint64_t revision_ = 0;
    std::uint64_t savedRevision_ = 0;
    std::uint64_t nextRevision_ = 1;
};

} // namespace forge::application
