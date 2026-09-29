#include "application/ModelDocument.h"
#include "domain/AdvancedFeature.h" // 类声明、快照类型和 NumericParameters。

#include "domain/Feature.h" // 析构、读取参数和调用虚函数都需要完整 Feature 定义。
#include "domain/ImportedFeature.h"
#include "domain/ExtrudeFeature.h"
#include "domain/ExtrudeCutFeature.h"
#include "geometry/ShapeFactory.h"
#include <BRepBuilderAPI_Copy.hxx>
#include "domain/BooleanFeature.h" // 创建和恢复 Cut/Union/Intersection 特征。
#include <QUuid>
#include <regex>
#include <limits>
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
#include <BRepTools.hxx>

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

    ModelDocument::ModelDocument()
    : documentId_(
          QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString())
    {
    }
    // 析构放在 .cpp，此处 Feature 已是完整类型，unique_ptr 才能正确实例化删除逻辑。
    ModelDocument::~ModelDocument() = default;

    domain::Feature& ModelDocument::createAdvancedFeature(const std::string& type,
        const domain::NumericParameters& parameters,domain::FeatureDefinition definition,const std::vector<std::string>& inputs)
    {
        const auto n=inputs.size();
        if (!domain::AdvancedFeature::isType(type) ||
            (type=="Loft" && (n<2 || n>32)) || (type=="Sweep" && n!=2) ||
            ((type=="Transform" || type=="Fillet" || type=="Chamfer") && n!=1) ||
            (!domain::AdvancedFeature::consumesInputs(type) && n!=0)) throw std::invalid_argument("特征类型或输入数量无效");
        const auto report=rebuildReport(); std::vector<TopoDS_Shape> shapes; std::unordered_set<std::string> unique;
        for (const auto& id:inputs) {
            if (!report.contains(id) || !unique.insert(id).second || report.at(id).status!=core::RebuildStatus::Ready)
                throw std::invalid_argument("输入不存在、重复或重建失败");
            shapes.push_back(report.at(id).shape);
        }
        domain::AdvancedFeature candidate("candidate",type,parameters,definition);
        const auto checked=candidate.rebuildResult(shapes);
        if (checked.status!=core::RebuildStatus::Ready) throw std::invalid_argument(checked.message);
        auto feature=std::make_unique<domain::AdvancedFeature>(nextFeatureId(type),type,parameters,std::move(definition));
        auto graph=dependencyGraph_; graph.addNode(feature->id());
        for (const auto& id:inputs) if (!graph.addDependency(feature->id(),id)) throw std::logic_error("无法登记输入");
        auto before=captureState(); features_.push_back(std::move(feature));dependencyGraph_=std::move(graph);
        rememberBeforeChange(std::move(before));return *features_.back();
    }

    void ModelDocument::setAdvancedDefinition(std::string_view id,const domain::NumericParameters& parameters,domain::FeatureDefinition definition)
    {
        auto* feature=dynamic_cast<domain::AdvancedFeature*>(findFeature(id));
        if (!feature) throw std::invalid_argument("此特征没有结构化定义");
        domain::AdvancedFeature candidate(feature->id(),feature->type(),parameters,definition);
        const auto report=rebuildReport();std::vector<TopoDS_Shape> shapes;
        for (const auto& input:dependenciesOf(id)) {
            if (report.at(input).status!=core::RebuildStatus::Ready) throw std::invalid_argument("上游重建失败");
            shapes.push_back(report.at(input).shape);
        }
        const auto checked=candidate.rebuildResult(shapes);
        if (checked.status!=core::RebuildStatus::Ready) throw std::invalid_argument(checked.message);
        auto before=captureState();
        for (const auto& p:candidate.parameters()) feature->setParameter(p.name(),p.asDouble());
        feature->setDefinition(std::move(definition));rememberBeforeChange(std::move(before));
    }

    // 创建流程：分配 ID -> Registry 创建对象 -> 领域校验 -> 保存旧状态 -> 加入文档。
    domain::Feature& ModelDocument::createFeature(
        const std::string& type,
        const domain::NumericParameters& parameters)
    {
        if (type=="Cone") return createAdvancedFeature(type,parameters);
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

    domain::Feature& ModelDocument::createExtrudeFeature(std::string_view sketchId, double height, domain::ExtrudeDirection direction)
    {
        const auto* sketch = findFeature(sketchId);
        if (!sketch || (sketch->type() != "RectangleSketch" && sketch->type() != "CircleSketch" && sketch->type() != "ProfileSketch" && sketch->type() != "Transform"))
            throw std::invalid_argument("拉伸需要一个现有矩形草图或圆形草图");
        const auto* rule = domain::FeatureRegistry::findParameter("Extrude","height");
        if (!std::isfinite(height) || height < rule->minimum || height > rule->maximum)
            throw std::invalid_argument("拉伸高度须在 1 到 10000 mm 之间");
        if (direction != domain::ExtrudeDirection::Forward && direction != domain::ExtrudeDirection::Reverse &&
            direction != domain::ExtrudeDirection::Symmetric)
            throw std::invalid_argument("未知拉伸方向");
        const auto report=rebuildReport();domain::ExtrudeFeature candidate("candidate",height,direction);
        const auto checked=candidate.rebuildResult({report.at(std::string(sketchId)).shape});
        if (checked.status!=core::RebuildStatus::Ready) throw std::invalid_argument(checked.message);
        auto feature = std::make_unique<domain::ExtrudeFeature>(nextFeatureId("Extrude"),height,direction);
        auto graph = dependencyGraph_;
        graph.addNode(feature->id());
        if (!graph.addDependency(feature->id(),std::string(sketchId)))
            throw std::logic_error("无法登记拉伸草图依赖");
        auto before = captureState();
        features_.push_back(std::move(feature));
        dependencyGraph_ = std::move(graph);
        rememberBeforeChange(std::move(before));
        return *features_.back();
    }

    domain::Feature& ModelDocument::createExtrudeCutFeature(std::string_view baseId,
        std::string_view sketchId, double height, domain::ExtrudeDirection direction)
    {
        const auto* sketch=findFeature(sketchId);
        if (baseId==sketchId || !findFeature(baseId) || !sketch ||
            (sketch->type()!="RectangleSketch" && sketch->type()!="CircleSketch" && sketch->type()!="ProfileSketch" && sketch->type()!="Transform"))
            throw std::invalid_argument("请选择实体主体和矩形或圆形草图");
        const auto* rule=domain::FeatureRegistry::findParameter("ExtrudeCut","height");
        if (!std::isfinite(height) || height<rule->minimum || height>rule->maximum)
            throw std::invalid_argument("切除高度须在 1 到 10000 mm 之间");
        domain::ExtrudeCutFeature candidate("",height,direction);
        const auto error=candidate.validate();
        if (!error.empty()) throw std::invalid_argument(error);
        const auto report=rebuildReport();
        const auto checked=candidate.rebuildResult({report.at(std::string(baseId)).shape,report.at(std::string(sketchId)).shape});
        if (!checked.usable()) throw std::invalid_argument(checked.message);
        auto feature=std::make_unique<domain::ExtrudeCutFeature>(nextFeatureId("ExtrudeCut"),height,direction);
        auto graph=dependencyGraph_;
        graph.addNode(feature->id());
        if (!graph.addDependency(feature->id(),std::string(baseId)) ||
            !graph.addDependency(feature->id(),std::string(sketchId)))
            throw std::logic_error("无法登记拉伸切除依赖");
        auto before=captureState();
        features_.push_back(std::move(feature));
        dependencyGraph_=std::move(graph);
        rememberBeforeChange(std::move(before));
        return *features_.back();
    }

    domain::Feature& ModelDocument::createRevolveFeature(std::string_view sketchId, double angle, domain::RevolveAxis axis)
    {
        const auto* sketch=findFeature(sketchId);
        if (!sketch || (sketch->type()!="RectangleSketch" && sketch->type()!="CircleSketch" && sketch->type()!="ProfileSketch" && sketch->type()!="Transform"))
            throw std::invalid_argument("旋转需要一个现有矩形或圆形草图");
        const auto* rule=domain::FeatureRegistry::findParameter("Revolve","angle");
        if (!std::isfinite(angle) || angle<rule->minimum || angle>rule->maximum)
            throw std::invalid_argument("旋转角度须在 0.001 到 360 度之间");
        domain::RevolveFeature candidate("",angle,axis);
        const auto error=candidate.validate();
        if (!error.empty()) throw std::invalid_argument(error);
        const auto report=rebuildReport();
        const auto geometry=candidate.rebuildResult({report.at(std::string(sketchId)).shape});
        if (!geometry.usable()) throw std::invalid_argument(geometry.message);
        auto feature=std::make_unique<domain::RevolveFeature>(nextFeatureId("Revolve"),angle,axis);
        auto graph=dependencyGraph_; graph.addNode(feature->id());
        if (!graph.addDependency(feature->id(),std::string(sketchId)))
            throw std::logic_error("无法登记旋转草图依赖");
        auto before=captureState(); features_.push_back(std::move(feature));
        dependencyGraph_=std::move(graph); rememberBeforeChange(std::move(before));
        return *features_.back();
    }

    void ModelDocument::addDependency(
        std::string_view featureId, std::string_view dependsOnId)
    {
        // 布尔特征的两个输入在创建时固定；额外依赖会破坏 base/tool 的位置。
        if (dynamic_cast<const domain::BooleanFeature*>(findFeature(featureId)) ||
            dynamic_cast<const domain::ExtrudeFeature*>(findFeature(featureId)) ||
            dynamic_cast<const domain::ExtrudeCutFeature*>(findFeature(featureId)) ||
            dynamic_cast<const domain::RevolveFeature*>(findFeature(featureId)) ||
            dynamic_cast<const domain::AdvancedFeature*>(findFeature(featureId))) {
            throw std::invalid_argument("此特征的输入只能在创建时指定");
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
    std::vector<std::string> ModelDocument::dependenciesOf(std::string_view featureId) const
    {
        if (!findFeature(featureId)) throw std::invalid_argument("找不到依赖查询对象");
        return dependencyGraph_.dependenciesOf(std::string(featureId));
    }

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

        for (const auto& parameter : feature->parameters())
            if (parameter.name() == parameterName && parameter.asDouble() == value) return;
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

    void ModelDocument::setPosition(std::string_view featureId, double x, double y, double z)
    {
        auto* feature=findFeature(featureId);
        if (!feature) throw std::invalid_argument("找不到定位对象");
        const domain::NumericParameters values{{"x",x},{"y",y},{"z",z}};
        bool changed=false;
        for (const auto& [name,value] : values) {
            const auto* rule=domain::FeatureRegistry::findParameter(feature->type(),name);
            if (!rule || !std::isfinite(value) || value<rule->minimum || value>rule->maximum)
                throw std::invalid_argument("对象不支持定位或位置超出范围");
            for (const auto& parameter : feature->parameters())
                if (parameter.name()==name && parameter.asDouble()!=value) changed=true;
        }
        if (!changed) return;
        auto before=captureState();
        try {
            for (const auto& [name,value] : values) feature->setParameter(name,value);
            const auto error=feature->validate();
            if (!error.empty()) throw std::invalid_argument(error);
        } catch (...) {
            restoreState(before);
            throw;
        }
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
            if (dynamic_cast<const domain::BooleanFeature*>(feature.get()) ||
                dynamic_cast<const domain::ExtrudeFeature*>(feature.get()) ||
                dynamic_cast<const domain::ExtrudeCutFeature*>(feature.get()) ||
                dynamic_cast<const domain::RevolveFeature*>(feature.get()) || domain::AdvancedFeature::consumesInputs(feature->type())) {
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
            } else if (dynamic_cast<const domain::BooleanFeature*>(findFeature(id)) ||
                       dynamic_cast<const domain::ExtrudeFeature*>(findFeature(id)) ||
                       dynamic_cast<const domain::ExtrudeCutFeature*>(findFeature(id)) ||
                       dynamic_cast<const domain::RevolveFeature*>(findFeature(id)) || domain::AdvancedFeature::consumesInputs(findFeature(id)->type())) {
                for (const auto& input : dependencyGraph_.dependenciesOf(id)) expose(input);
            }
        };
        for (const auto& feature : features_) if (!consumed.contains(feature->id())) expose(feature->id());
        std::vector<std::string> ordered;
        for (const auto& feature : features_) if (visible.contains(feature->id())) ordered.push_back(feature->id());
        return ordered;
    }

    DocumentData ModelDocument::exportData() const {
        DocumentData data;
        data.documentId = documentId_;
        data.sequences = sequenceByType_;
        data.features.reserve(features_.size());

        for (const auto& feature : features_) {
            FeatureData item;
            item.id = feature->id();
            item.type = feature->type();

            // 保存所有数值参数，包括位置。
            for (const auto& parameter : feature->parameters()) {
                item.parameters.emplace(parameter.name(), parameter.asDouble());
            }

            // 保留依赖顺序：布尔运算的主体在前、工具在后。
            item.dependencies =
                dependencyGraph_.dependenciesOf(feature->id());

            if (const auto* imported =
                    dynamic_cast<const domain::ImportedFeature*>(feature.get())) {
                item.sourceName = imported->sourceName();
                item.geometryAsset = "shapes/" + item.id + ".brep";

                const auto& geometry = imported->geometry();
                if (!geometry || geometry->IsNull()) {
                    throw std::runtime_error("导入特征缺少原始几何");
                }

                std::ostringstream stream;
                BRepTools::Write(*geometry, stream);
                if (!stream || stream.str().empty()) {
                    throw std::runtime_error("导入几何编码失败");
                }

                data.geometryAssets.emplace(item.geometryAsset, stream.str());
                    }

            if (const auto* advanced=dynamic_cast<const domain::AdvancedFeature*>(feature.get())) item.definition=advanced->definition();
            data.features.push_back(std::move(item));
        }

        return data;
    }

    void ModelDocument::replaceData(const DocumentData& data)
    {
        if (data.version != 1 || data.units != "mm" ||
            QUuid(QString::fromStdString(data.documentId)).isNull() || data.features.size() > 10000)
            throw std::invalid_argument("文档版本、单位或身份无效");
        DocumentState state;
        std::unordered_set<std::string> usedAssets;
        const std::unordered_set<std::string> types{"Box","Cylinder","Sphere","Cut","Union","Intersection","Imported","RectangleSketch","CircleSketch","Extrude","ExtrudeCut","Revolve","Cone","ProfileSketch","Path3D","Transform","Fillet","Chamfer","Sweep","Loft"};
        for (const auto& [type,n] : data.sequences)
            if (!types.contains(type) || n < 0 || n >= std::numeric_limits<int>::max())
                throw std::invalid_argument("特征编号计数无效");
        std::unordered_set<std::string> ids;
        for (const auto& f : data.features) {
            if (!types.contains(f.type) || f.version != 1 || !ids.insert(f.id).second ||
                f.id.size() > 64 || !std::regex_match(f.id,std::regex(f.type + "[0-9]{3,}")))
                throw std::invalid_argument("特征类型、版本或 ID 无效");
            const auto suffix = f.id.substr(f.type.size());
            const auto number = std::stoll(suffix);
            if (number < 1 || !data.sequences.contains(f.type) || number > data.sequences.at(f.type))
                throw std::invalid_argument("特征编号计数落后于现有 ID");
            state.graph.addNode(f.id);
            FeatureState saved{f.id,f.type,f.parameters};
            saved.definition=f.definition;
            if (!domain::AdvancedFeature::isType(f.type) && !f.definition.empty()) throw std::invalid_argument("Unexpected structured definition");
            if (f.type == "Imported") {
                if (f.geometryAsset != "shapes/" + f.id + ".brep" || !data.geometryAssets.contains(f.geometryAsset) ||
                    !usedAssets.insert(f.geometryAsset).second || f.parameters.size() != 3)
                    throw std::invalid_argument("导入几何资源或位置参数无效");
                for (const auto* name : {"x","y","z"}) {
                    const auto* descriptor = domain::FeatureRegistry::findParameter("Imported",name);
                    const auto value = f.parameters.at(name);
                    if (!std::isfinite(value) || value < descriptor->minimum || value > descriptor->maximum)
                        throw std::invalid_argument("导入位置超出范围");
                }
                const auto& bytes = data.geometryAssets.at(f.geometryAsset);
                if (bytes.size() > 64 * 1024 * 1024) throw std::invalid_argument("几何资源过大");
                std::istringstream stream(bytes);
                TopoDS_Shape shape;
                BRepTools::Read(shape,stream,BRep_Builder());
                if (stream.fail() || geometry::ShapeFactory::inspectShape(shape).status != core::RebuildStatus::Ready)
                    throw std::invalid_argument("导入 BRep 无效");
                saved.importedGeometry = std::make_shared<const TopoDS_Shape>(shape);
                saved.sourceName = f.sourceName;
            } else {
                if (!f.geometryAsset.empty() || !f.sourceName.empty()) throw std::invalid_argument("非导入特征不能包含几何资源");
                const bool boolean = f.type == "Cut" || f.type == "Union" || f.type == "Intersection";
                if (domain::AdvancedFeature::isType(f.type)) {
                    const auto n=f.dependencies.size();
                    if ((f.type=="Loft" && (n<2 || n>32)) || (f.type=="Sweep" && n!=2) ||
                        ((f.type=="Transform" || f.type=="Fillet" || f.type=="Chamfer") && n!=1) ||
                        (!domain::AdvancedFeature::consumesInputs(f.type) && n!=0)) throw std::invalid_argument("Advanced input count invalid");
                    domain::AdvancedFeature candidate(f.id,f.type,f.parameters,f.definition);
                    saved.parameters.clear();
                    for (const auto& p:candidate.parameters()) saved.parameters.emplace(p.name(),p.asDouble());
                } else if (f.type == "Extrude" || f.type == "ExtrudeCut") {
                    const bool cut=f.type=="ExtrudeCut";
                    if (f.dependencies.size() != (cut ? 2u : 1u) || !f.parameters.contains("height") ||
                        f.parameters.size() != (f.parameters.contains("direction") ? 2u : 1u))
                        throw std::invalid_argument("拉伸必须包含一个草图输入和高度参数");
                    const auto input = std::find_if(data.features.begin(),data.features.end(),
                        [&](const auto& other) { return other.id == f.dependencies[cut ? 1 : 0]; });
                    if (input == data.features.end() || (input->type != "RectangleSketch" && input->type != "CircleSketch" && input->type != "ProfileSketch" && input->type != "Transform"))
                        throw std::invalid_argument("拉伸输入必须是矩形草图或圆形草图");
                    if (cut) {
                        const auto base=std::find_if(data.features.begin(),data.features.end(),
                            [&](const auto& other) { return other.id==f.dependencies[0]; });
                        if (base==data.features.end() || base->type=="RectangleSketch" || base->type=="CircleSketch")
                            throw std::invalid_argument("拉伸切除主体必须是实体特征");
                    }
                    const auto* rule = domain::FeatureRegistry::findParameter("Extrude","height");
                    const auto height = f.parameters.at("height");
                    if (!std::isfinite(height) || height < rule->minimum || height > rule->maximum)
                        throw std::invalid_argument("拉伸高度超出范围");
                    const auto direction = f.parameters.contains("direction") ? f.parameters.at("direction") : 0.0;
                    if (direction != 0.0 && direction != 1.0 && direction != 2.0)
                        throw std::invalid_argument("未知拉伸方向");
                } else if (f.type=="Revolve") {
                    if (f.dependencies.size()!=1 ||
                        !f.parameters.contains("angle") || !f.parameters.contains("axis"))
                        throw std::invalid_argument("旋转定义必须包含一个草图输入、角度和旋转轴");
                    const auto input=std::find_if(data.features.begin(),data.features.end(),
                        [&](const auto& other) { return other.id==f.dependencies[0]; });
                    if (input==data.features.end() || (input->type!="RectangleSketch" && input->type!="CircleSketch" && input->type!="ProfileSketch" && input->type!="Transform"))
                        throw std::invalid_argument("旋转输入必须是草图");
                    const auto angle=f.parameters.at("angle"), axis=f.parameters.at("axis");
                    const auto* rule=domain::FeatureRegistry::findParameter("Revolve","angle");
                    if (!std::isfinite(angle) || angle<rule->minimum || angle>rule->maximum || (axis!=0 && axis!=1 && axis!=2))
                        throw std::invalid_argument("旋转角度或轴无效");
                    // 兼容旧文件只有 angle/axis；可选的结果平移缺省为0，未知字段仍拒绝。
                    for (const auto& [name,value] : f.parameters) {
                        const auto* descriptor=domain::FeatureRegistry::findParameter("Revolve",name);
                        if (!descriptor || !std::isfinite(value) || value<descriptor->minimum || value>descriptor->maximum)
                            throw std::invalid_argument("旋转参数无效: " + name);
                    }
                    for (const auto* name : {"x","y","z"})
                        if (!saved.parameters.contains(name)) saved.parameters[name]=0;
                } else if (boolean) {
                    if (!f.parameters.empty() || f.dependencies.size() != 2) throw std::invalid_argument("布尔定义无效");
                } else {
                    const auto* descriptor = domain::FeatureRegistry::find(f.type);
                    // 旧版本草图没有 Z；保持原来的 Z=0，其他字段仍严格校验。
                    if ((f.type=="RectangleSketch" || f.type=="CircleSketch") && !saved.parameters.contains("z"))
                        saved.parameters["z"]=0;
                    if ((f.type=="RectangleSketch" || f.type=="CircleSketch") && !saved.parameters.contains("plane")) saved.parameters["plane"]=0;
                    if (saved.parameters.size() != descriptor->parameters.size()) throw std::invalid_argument("缺少完整特征参数");
                    domain::FeatureRegistry::create(f.type,f.id,saved.parameters);
                }
            }
            state.features.push_back(std::move(saved));
        }
        if (usedAssets.size() != data.geometryAssets.size()) throw std::invalid_argument("存在未引用几何资源");
        for (const auto& f : data.features)
            for (const auto& input : f.dependencies)
                if (!state.graph.addDependency(f.id,input)) throw std::invalid_argument("依赖缺失、重复或形成循环");
        // 所有可能分配内存的准备工作在替换前完成。
        auto sequences = data.sequences;
        auto identity = data.documentId;
        restoreState(state);
        sequenceByType_.swap(sequences);
        documentId_.swap(identity);
        undoStack_.clear(); redoStack_.clear();
        revision_ = nextRevision_++;
        savedRevision_ = revision_;
    }

    void ModelDocument::swap(ModelDocument& other) noexcept
    {
        features_.swap(other.features_);
        std::swap(dependencyGraph_,other.dependencyGraph_);
        sequenceByType_.swap(other.sequenceByType_);
        undoStack_.swap(other.undoStack_); redoStack_.swap(other.redoStack_);
        documentId_.swap(other.documentId_);
        std::swap(revision_,other.revision_);
        std::swap(savedRevision_,other.savedRevision_);
        std::swap(nextRevision_,other.nextRevision_);
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
        state.revision = revision_;
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
            if (const auto* advanced=dynamic_cast<const domain::AdvancedFeature*>(feature.get())) saved.definition=advanced->definition();
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
            if (domain::AdvancedFeature::isType(saved.type)) {
                restored.push_back(std::make_unique<domain::AdvancedFeature>(saved.id,saved.type,saved.parameters,saved.definition));
            } else if (saved.type == "Imported") {
                restored.push_back(std::make_unique<domain::ImportedFeature>(saved.id, saved.importedGeometry,
                    saved.sourceName, saved.parameters.at("x"), saved.parameters.at("y"), saved.parameters.at("z")));
            } else if (saved.type=="Revolve") {
                restored.push_back(std::make_unique<domain::RevolveFeature>(saved.id,saved.parameters.at("angle"),
                    static_cast<domain::RevolveAxis>(static_cast<int>(saved.parameters.at("axis"))),
                    saved.parameters.at("x"),saved.parameters.at("y"),saved.parameters.at("z")));
            } else if (saved.type == "ExtrudeCut") {
                const double direction=saved.parameters.contains("direction") ? saved.parameters.at("direction") : 0.0;
                restored.push_back(std::make_unique<domain::ExtrudeCutFeature>(saved.id,saved.parameters.at("height"),
                    static_cast<domain::ExtrudeDirection>(static_cast<int>(direction))));
            } else if (saved.type == "Extrude") {
                const double direction = saved.parameters.contains("direction") ? saved.parameters.at("direction") : 0.0;
                restored.push_back(std::make_unique<domain::ExtrudeFeature>(saved.id,saved.parameters.at("height"),
                    static_cast<domain::ExtrudeDirection>(static_cast<int>(direction))));
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
        revision_ = state.revision;
    }

    void ModelDocument::rememberBeforeChange(DocumentState state)
    {
        // 用户在 Undo 后做出新修改时，旧 Redo 分支已经不再属于当前时间线。
        undoStack_.push_back(std::move(state)); // vector 末尾作为最新可撤销状态。
        redoStack_.clear(); // 新操作形成新时间线，旧 Redo 路径不再有效。
        revision_ = nextRevision_++;
    }

} // namespace forge::application
