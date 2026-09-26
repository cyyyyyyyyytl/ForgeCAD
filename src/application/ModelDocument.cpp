#include "application/ModelDocument.h" // 类声明、快照类型和 NumericParameters。

#include "domain/Feature.h" // 析构、读取参数和调用虚函数都需要完整 Feature 定义。
#include "domain/ImportedFeature.h"
#include "geometry/ShapeFactory.h"
#include <BRepBuilderAPI_Copy.hxx>
#include "domain/BooleanFeature.h" // 创建和恢复 Cut/Union/Intersection 特征。

#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <Standard_Failure.hxx>
#include <functional>
#include <cmath>
#include <algorithm> // std::find_if 按稳定 ID 在线性容器中查找对象。
#include <iomanip>   // setw/setfill 把序号格式化成三位数。
#include <sstream>  // ostringstream 拼接类型名和格式化后的序号。
#include <stdexcept> // invalid_argument 报告不存在对象和非法业务参数。
#include <utility>   // std::move 转移 unique_ptr 和快照容器，避免深复制。
#include <unordered_set> // 收集被布尔特征使用、需要隐藏的输入 ID。

namespace forge::application {

    namespace {

    std::string_view booleanTypeName(domain::BooleanOperation operation)
    {
        switch (operation) {
        case domain::BooleanOperation::Difference: return "Cut";
        case domain::BooleanOperation::Union: return "Union";
        case domain::BooleanOperation::Intersection: return "Intersection";
        }
        throw std::invalid_argument("未知布尔运算");
    }

    } // namespace

    // 析构放在 .cpp，此处 Feature 已是完整类型，unique_ptr 才能正确实例化删除逻辑。
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
        dependencyGraph_.addNode(id);
        rememberBeforeChange(std::move(before));
        // vector 保存的是 unique_ptr；返回的是它所指向 Feature 的非拥有引用。
        return *features_.back(); // 解引用最后一个 unique_ptr，只借出对象引用，不转移所有权。
    }

    domain::Feature& ModelDocument::createImportedFeature(const TopoDS_Shape& shape, const std::string& sourceName)
    {
        const auto checked = geometry::ShapeFactory::inspectShape(shape);
        if (checked.status != core::RebuildStatus::Ready) throw std::invalid_argument("不能导入此形状：" + checked.message);
        // 复制输入，后续原文件、调用方或读取器变化都不能破坏文档与历史中的几何。
        BRepBuilderAPI_Copy copy(shape, true, false);
        auto geometry = std::make_shared<const TopoDS_Shape>(copy.Shape());
        auto feature = std::make_unique<domain::ImportedFeature>(nextFeatureId("Imported"), geometry, sourceName);
        DocumentState before = captureState();
        auto updatedGraph = dependencyGraph_;
        updatedGraph.addNode(feature->id());
        features_.push_back(std::move(feature));
        dependencyGraph_ = std::move(updatedGraph);
        rememberBeforeChange(std::move(before));
        return *features_.back();
    }

    domain::Feature& ModelDocument::createBooleanFeature(
        domain::BooleanOperation operation,
        std::string_view baseId,
        std::string_view toolId)
    {
        // 先检查两个现有对象，再生成 ID；失败不会改变文档或 Undo 历史。
        if (baseId == toolId || !findFeature(baseId) || !findFeature(toolId)) {
            throw std::invalid_argument("布尔特征需要两个不同的现有特征");
        }

        const std::string id = nextFeatureId(booleanTypeName(operation));
        auto feature = std::make_unique<domain::BooleanFeature>(id, operation);

        // 在副本中把两条边按 base、tool 顺序加好，避免只添加一条边的半成品。
        domain::DependencyGraph updatedGraph = dependencyGraph_;
        updatedGraph.addNode(id);
        if (!updatedGraph.addDependency(id, std::string(baseId)) ||
            !updatedGraph.addDependency(id, std::string(toolId))) {
            throw std::logic_error("无法登记布尔特征依赖关系");
        }

        DocumentState before = captureState();
        features_.push_back(std::move(feature));
        dependencyGraph_ = std::move(updatedGraph);
        rememberBeforeChange(std::move(before));
        return *features_.back();
    }

    void ModelDocument::addDependency(
        std::string_view featureId, std::string_view dependsOnId)
    {
        // 布尔特征的两个输入在创建时固定；额外依赖会破坏 base/tool 的位置。
        if (dynamic_cast<const domain::BooleanFeature*>(findFeature(featureId))) {
            throw std::invalid_argument("布尔特征的输入只能在创建时指定");
        }
        // 图负责拒绝未知节点、重复关系和循环；失败时文档与历史都保持原样。
        DocumentState before = captureState();
        if (!dependencyGraph_.addDependency(
                std::string(featureId), std::string(dependsOnId))) {
            throw std::invalid_argument("无法添加特征依赖关系");
        }
        rememberBeforeChange(std::move(before));
    }

    // 参数修改流程：定位对象 -> 查参数规则 -> 保存旧状态 -> 修改 -> 领域校验。
    void ModelDocument::setParameter(std::string_view featureId,std::string_view parameterName,double value)
    {
        // 稳定 ID 是 UI、AI、历史恢复共同使用的对象身份。
        domain::Feature* feature = findFeature(featureId);
        if (!feature) {
            throw std::invalid_argument("找不到 Feature: " + std::string(featureId)); // 不产生历史。
        }

        // Registry 是参数名、最小值、最大值的唯一说明来源。
        const domain::ParameterDescriptor* descriptor =
            domain::FeatureRegistry::findParameter(feature->type(), parameterName);
        if (!descriptor) {
            throw std::invalid_argument(
                feature->type() + " 没有参数: " + std::string(parameterName));
        }
        if (!std::isfinite(value) || value < descriptor->minimum || value > descriptor->maximum) {
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

        // 先保存完整模型及依赖图；整组级联删除只占一条 Undo 历史。
        DocumentState before = captureState();
        const auto removed = dependencyGraph_.removeNodeAndDependents(std::string(featureId));

        for (const std::string& id : removed) {
            std::erase_if(features_, [&id](const auto& feature) {
                return feature->id() == id;
            });
        }
        rememberBeforeChange(std::move(before));
    }

    std::vector<std::string> ModelDocument::deletionOrder(
        std::string_view featureId) const
    {
        if (!findFeature(featureId)) {
            throw std::invalid_argument(
                "找不到 Feature: " + std::string(featureId));
        }

        return dependencyGraph_.deletionOrder(std::string(featureId));
    }

    void ModelDocument::undo()
    {
        if (undoStack_.empty()) {
            return; // 空栈时撤销是安全空操作，调用方无需捕获异常。
        }

        // 当前状态稍后可用于 Redo；Undo 栈顶则是需要恢复的上一个状态。
        DocumentState current = captureState();
        DocumentState previous = std::move(undoStack_.back());
        undoStack_.pop_back();                     // 移出后删除已使用的 Undo 栈顶槽位。
        restoreState(previous);                    // 用上一个状态替换当前文档内容。
        redoStack_.push_back(std::move(current));  // 保存刚离开的状态，供 Redo 返回。
    }

    void ModelDocument::redo()
    {
        if (redoStack_.empty()) {
            return; // 没有未来状态时重做同样是安全空操作。
        }

        // Redo 与 Undo 对称：保存当前状态，再恢复 Redo 栈顶。
        DocumentState current = captureState();
        DocumentState next = std::move(redoStack_.back());
        redoStack_.pop_back();                    // 删除已经取出的 Redo 栈顶槽位。
        restoreState(next);                       // 前进到此前撤销掉的状态。
        undoStack_.push_back(std::move(current)); // 保存出发点，使这次 Redo 还能再次 Undo。
    }

    bool ModelDocument::canUndo() const
    {
        return !undoStack_.empty(); // 取反后 true 表示至少有一个可恢复的过去状态。
    }

    bool ModelDocument::canRedo() const
    {
        return !redoStack_.empty(); // true 表示当前时间线存在可重新应用的未来状态。
    }

    domain::Feature* ModelDocument::findFeature(std::string_view id)
    {
        // 当前 MVP 特征数量少，线性查找最直观；性能需要时可加 ID 索引而不改 API。
        const auto it = std::find_if(
            features_.begin(), features_.end(),
            [id](const auto& feature) { return feature->id() == id; });
        // get() 只借出裸指针，unique_ptr 和对象所有权仍留在 features_ 中。
        return it == features_.end() ? nullptr : it->get();
    }

    const domain::Feature* ModelDocument::findFeature(std::string_view id) const
    {
        const auto it = std::find_if(
            features_.begin(), features_.end(),
            [id](const auto& feature) { return feature->id() == id; });
        // const 重载返回 const Feature*，只读调用方不能绕过 Document 修改对象。
        return it == features_.end() ? nullptr : it->get();
    }

    const std::vector<std::unique_ptr<domain::Feature>>& ModelDocument::features() const
    {
        return features_; // 返回 const 引用：不复制容器，也禁止调用方增删 unique_ptr。
    }

    ModelDocument::RebuildReport ModelDocument::rebuildReport() const
    {
        RebuildReport report;
        report.reserve(features_.size());
        for (const auto& id : dependencyGraph_.topologicalOrder()) {
            const auto* feature = findFeature(id);
            if (!feature) throw std::logic_error("依赖图中的特征不存在: " + id);
            std::vector<TopoDS_Shape> inputs;
            std::string failedInputs;
            for (const auto& inputId : dependencyGraph_.dependenciesOf(id)) {
                const auto& input = report.at(inputId);
                inputs.push_back(input.shape);
                if (!input.usable()) {
                    if (!failedInputs.empty()) failedInputs += "、";
                    failedInputs += inputId;
                }
            }
            if (!failedInputs.empty()) {
                report.emplace(id, core::ShapeResult{{}, core::RebuildStatus::Blocked,
                    "输入特征失败：" + failedInputs + "。请先修复上游特征"});
                continue;
            }
            // 每个特征单独捕获计算异常，独立分支仍能继续重建。
            try {
                report.emplace(id, feature->rebuildResult(inputs));
            } catch (const Standard_Failure& error) {
                report.emplace(id, core::ShapeResult{{}, core::RebuildStatus::Failed,
                    std::string("几何内核异常：") + (error.GetMessageString() ? error.GetMessageString() : "未知原因")});
            } catch (const std::exception& error) {
                report.emplace(id, core::ShapeResult{{}, core::RebuildStatus::Failed,
                    std::string("重建异常：") + error.what()});
            }
        }
        if (report.size() != features_.size()) throw std::logic_error("依赖图与文档特征数量不一致");
        return report;
    }

    core::ShapeResult ModelDocument::shapeForExport() const
    {
        const auto report = rebuildReport();
        std::string errors;
        // 按文档顺序列出问题，提示顺序不依赖 unordered_map 的遍历顺序。
        for (const auto& feature : features_) {
            const auto& result = report.at(feature->id());
            if (!result.usable()) errors += feature->id() + "：" + result.message + "\n";
        }
        if (!errors.empty()) return {{}, core::RebuildStatus::Failed, "请先修复以下特征，再导出：\n" + errors};

        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        int count = 0, emptyCount = 0;
        for (const auto& id : visibleFeatureIds(report)) {
            const auto& result = report.at(id);
            if (result.status == core::RebuildStatus::Empty) {
                ++emptyCount;
            } else {
                builder.Add(compound, result.shape);
                ++count;
            }
        }
        if (count == 0) return {compound, core::RebuildStatus::Empty,
            features_.empty() ? "文档为空，没有可导出的几何" : "所有最终结果均为空，没有可导出的几何"};
        return {compound, core::RebuildStatus::Ready,
            "最终结果 " + std::to_string(count) + " 个，跳过 " + std::to_string(emptyCount) + " 个空结果"};
    }

    std::unordered_map<std::string, TopoDS_Shape> ModelDocument::rebuildShapes() const
    {
        std::unordered_map<std::string, TopoDS_Shape> shapes;
        for (const auto& [id, result] : rebuildReport()) shapes.emplace(id, result.shape);
        return shapes;
    }

    std::vector<std::string> ModelDocument::visibleFeatureIds() const
    {
        return visibleFeatureIds(rebuildReport());
    }

    std::vector<std::string> ModelDocument::visibleFeatureIds(const RebuildReport& report) const
    {
        std::unordered_set<std::string> consumed;
        for (const auto& feature : features_) {
            if (dynamic_cast<const domain::BooleanFeature*>(feature.get())) {
                for (const auto& id : dependencyGraph_.dependenciesOf(feature->id())) consumed.insert(id);
            }
        }
        std::unordered_set<std::string> visible;
        std::unordered_set<std::string> visited;
        std::function<void(const std::string&)> expose = [&](const std::string& id) {
            if (!visited.insert(id).second) return;
            const auto& result = report.at(id);
            if (result.usable()) {
                // 合法空结果继续隐藏输入，不能把空交集伪装成输入模型。
                visible.insert(id);
            } else if (dynamic_cast<const domain::BooleanFeature*>(findFeature(id))) {
                for (const auto& input : dependencyGraph_.dependenciesOf(id)) expose(input);
            }
        };
        for (const auto& feature : features_) if (!consumed.contains(feature->id())) expose(feature->id());
        std::vector<std::string> ordered;
        for (const auto& feature : features_) if (visible.contains(feature->id())) ordered.push_back(feature->id());
        return ordered;
    }

    std::string ModelDocument::nextFeatureId(std::string_view type)
    {
        const std::string typeName(type);
        // map 的 operator[] 会在类型首次出现时建立值为 0 的计数器。
        auto& sequence = sequenceByType_[typeName]; // 引用 map 中计数，++ 会直接写回文档。
        std::string candidate;
        do {
            std::ostringstream stream;
            stream << typeName << std::setw(3) << std::setfill('0') << ++sequence;
            candidate = stream.str(); // 例如 typeName=Box、sequence=1 得到 Box001。
        } while (findFeature(candidate)); // 兼容未来导入带现成 ID 的文档。
        return candidate; // 返回已确认没有与现有 Feature 冲突的稳定 ID。
    }

    ModelDocument::DocumentState ModelDocument::captureState() const
    {
        // 深拷贝的是轻量字符串和数值，不复制 unique_ptr，也不计算 OCCT Shape。
        DocumentState state;
        state.features.reserve(features_.size()); // 已知最终项数，预留空间避免 push 时重复扩容。
        for (const auto& feature : features_) {
            FeatureState saved{feature->id(), feature->type(), {}};
            for (const auto& parameter : feature->parameters()) {
                // 只保存能重建 Feature 的业务数据，不保存指针或派生几何缓存。
                saved.parameters.emplace(parameter.name(), parameter.asDouble());
            }
            if (const auto* imported = dynamic_cast<const domain::ImportedFeature*>(feature.get())) {
                saved.importedGeometry = imported->geometry();
                saved.sourceName = imported->sourceName();
            }
            state.features.push_back(std::move(saved)); // 移入快照并保留与当前文档一致的对象顺序。
        }
        state.graph = dependencyGraph_;
        return state; // 返回独立深拷贝；之后修改当前文档不会改变这张照片。
    }

    void ModelDocument::restoreState(const DocumentState& state)
    {
        // 先根据快照里的模型数据，重新创建全部 Feature。
        std::vector<std::unique_ptr<domain::Feature>> restored;
        restored.reserve(state.features.size());

        for (const auto& saved : state.features) {
            // 基础体由 Registry 恢复；布尔特征的输入关系存放在 graph 中。
            if (saved.type == "Imported") {
                restored.push_back(std::make_unique<domain::ImportedFeature>(saved.id, saved.importedGeometry,
                    saved.sourceName, saved.parameters.at("x"), saved.parameters.at("y"), saved.parameters.at("z")));
            } else if (saved.type == "Cut") {
                restored.push_back(std::make_unique<domain::BooleanFeature>(
                    saved.id, domain::BooleanOperation::Difference));
            } else if (saved.type == "Union") {
                restored.push_back(std::make_unique<domain::BooleanFeature>(
                    saved.id, domain::BooleanOperation::Union));
            } else if (saved.type == "Intersection") {
                restored.push_back(std::make_unique<domain::BooleanFeature>(
                    saved.id, domain::BooleanOperation::Intersection));
            } else {
                restored.push_back(domain::FeatureRegistry::create(
                    saved.type, saved.id, saved.parameters));
            }
        }

        // 也先准备好快照中的依赖图。
        domain::DependencyGraph restoredGraph = state.graph;

        // 两份数据都准备好后，再替换文档当前状态。
        features_ = std::move(restored);
        dependencyGraph_ = std::move(restoredGraph);
    }

    void ModelDocument::rememberBeforeChange(DocumentState state)
    {
        // 用户在 Undo 后做出新修改时，旧 Redo 分支已经不再属于当前时间线。
        undoStack_.push_back(std::move(state)); // vector 末尾作为最新可撤销状态。
        redoStack_.clear(); // 新操作形成新时间线，旧 Redo 路径不再有效。
    }

} // namespace forge::application
