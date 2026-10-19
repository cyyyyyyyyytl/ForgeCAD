#include "application/ModelDocument.h"
#include "application/ModelCommands.h" // 文档把每次修改封装成命令，交由 CommandManager 记录。
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
#include <cstddef>   // std::size_t/std::ptrdiff_t：文档顺序下标与 vector 插入位置。
#include <charconv>  // from_chars 用于受控解析 ID 后缀，避免 stoll 抛出 out_of_range。
#include <system_error> // std::errc 是 from_chars 的错误码类型。
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

    // 解析形如 Box001 的 ID 后缀。
    // 用 from_chars 做 checked 解析：畸形文件里的超长数字应作为业务数据错误
    // 统一抛 invalid_argument，而不是把 std::stoll 的 out_of_range 泄漏成实现细节。
    int checkedIdSuffix(std::string_view suffix)
    {
        int number = 0;
        const auto parsed = std::from_chars(
            suffix.data(), suffix.data() + suffix.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size()) {
            throw std::invalid_argument("特征 ID 编号无效");
        }
        return number;
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
        // 先用候选对象完成几何验证，再创建正式对象并交给“创建命令”。
        // 命令持有对象直到首次 execute，因此失败时文档和 ID 序号都不会改变。
        auto feature=std::make_unique<domain::AdvancedFeature>(nextFeatureId(type),type,parameters,std::move(definition));
        const std::string id=feature->id();
        const std::size_t index=features_.size();
        runCommand(std::make_unique<CreateFeatureCommand>(std::move(feature),inputs,index));
        return *findFeatureForWrite(id);
    }

    void ModelDocument::setAdvancedDefinition(std::string_view id,const domain::NumericParameters& parameters,domain::FeatureDefinition definition)
    {
        auto* feature=dynamic_cast<domain::AdvancedFeature*>(findFeatureForWrite(id));
        if (!feature) throw std::invalid_argument("此特征没有结构化定义");
        domain::AdvancedFeature candidate(feature->id(),feature->type(),parameters,definition);
        const auto report=rebuildReport();std::vector<TopoDS_Shape> shapes;
        for (const auto& input:dependenciesOf(id)) {
            if (report.at(input).status!=core::RebuildStatus::Ready) throw std::invalid_argument("上游重建失败");
            shapes.push_back(report.at(input).shape);
        }
        const auto checked=candidate.rebuildResult(shapes);
        if (checked.status!=core::RebuildStatus::Ready) throw std::invalid_argument(checked.message);

        // 把“多个参数 + 结构化定义”的变化打包成一条组合命令：
        // 一次编辑在历史上仍只占一步，但每条子命令只保存自己改动的数据，
        // 不再复制整份文档。没有实际变化的编辑不产生历史。
        auto composite=std::make_unique<CompositeCommand>();
        for (const auto& parameter:candidate.parameters()) {
            double oldValue=0.0;
            for (const auto& current:feature->parameters())
                if (current.name()==parameter.name()) oldValue=current.asDouble();
            const double newValue=parameter.asDouble();
            if (oldValue!=newValue)
                composite->add(std::make_unique<ModifyParameterCommand>(feature->id(),parameter.name(),oldValue,newValue));
        }
        if (!(feature->definition()==definition))
            composite->add(std::make_unique<SetDefinitionCommand>(feature->id(),feature->definition(),definition));
        if (!composite->empty()) runCommand(std::move(composite));
    }

    // 创建流程：分配 ID -> Registry 创建对象 -> 领域校验 -> 生成创建命令 -> 由历史栈执行并记录。
    domain::Feature& ModelDocument::createFeature(
        const std::string& type,
        const domain::NumericParameters& parameters)
    {
        if (type=="Cone") return createAdvancedFeature(type,parameters);
        // nextFeatureId 会先写入序号计数，Registry 随后可能拒绝未知类型；
        // 若不恢复，未登记类型（如 Torus）会留在 sequenceByType_ 里，
        // 使 exportData() 生成 replaceData() 自己都会拒绝的序号表，导致文档无法保存。
        auto sequencesBefore = sequenceByType_;
        try {
            // ID 由文档统一产生，避免 UI 与 AI 各自编号后发生冲突。
            const std::string id = nextFeatureId(type);
            // Registry 将字符串类型和具名参数转换成具体的 Box/Cylinder/Sphere 对象。
            auto feature = domain::FeatureRegistry::create(type, id, parameters);

            // Registry 检查外部参数格式；Feature::validate() 再保护对象自身不变量。
            const std::string validationError = feature->validate();
            if (!validationError.empty()) {
                throw std::invalid_argument(validationError);
            }

            // 校验通过后才生成命令；命令执行失败时历史不会留下半条记录。
            // 命令持有 unique_ptr，撤销时取回对象、重做时交还文档，ID/地址保持稳定。
            const std::size_t index = features_.size();
            runCommand(std::make_unique<CreateFeatureCommand>(
                std::move(feature), std::vector<std::string>{}, index));
            // 命令执行成功后对象已进入 features_；返回非拥有引用给调用方。
            return *findFeatureForWrite(id);
        } catch (...) {
            // 失败创建必须像从未发生：恢复序号表，且 features_/依赖图/历史/revision 都没有改动。
            sequenceByType_ = std::move(sequencesBefore);
            throw;
        }
    }

    domain::Feature& ModelDocument::createImportedFeature(const TopoDS_Shape& shape, const std::string& sourceName)
    {
        const auto checked = geometry::ShapeFactory::inspectShape(shape);
        if (checked.status != core::RebuildStatus::Ready) throw std::invalid_argument("不能导入此形状：" + checked.message);
        // 复制输入，后续原文件、调用方或读取器变化都不能破坏文档与历史中的几何。
        BRepBuilderAPI_Copy copy(shape, true, false);
        auto geometry = std::make_shared<const TopoDS_Shape>(copy.Shape());
        auto feature = std::make_unique<domain::ImportedFeature>(nextFeatureId("Imported"), geometry, sourceName);
        const std::string id = feature->id();
        const std::size_t index = features_.size();
        runCommand(std::make_unique<CreateFeatureCommand>(std::move(feature),
            std::vector<std::string>{}, index));
        return *findFeatureForWrite(id);
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
        // 两个输入的顺序会成为依赖图里的边顺序，命令重做时按同一顺序恢复。
        const std::vector<std::string> inputs{std::string(baseId), std::string(toolId)};
        const std::size_t index = features_.size();
        runCommand(std::make_unique<CreateFeatureCommand>(std::move(feature), inputs, index));
        return *findFeatureForWrite(id);
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
        const std::string id = feature->id();
        const std::size_t index = features_.size();
        runCommand(std::make_unique<CreateFeatureCommand>(std::move(feature),
            std::vector<std::string>{std::string(sketchId)}, index));
        return *findFeatureForWrite(id);
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
        const std::string id = feature->id();
        const std::vector<std::string> inputs{std::string(baseId), std::string(sketchId)};
        const std::size_t index = features_.size();
        runCommand(std::make_unique<CreateFeatureCommand>(std::move(feature), inputs, index));
        return *findFeatureForWrite(id);
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
        const std::string id = feature->id();
        const std::size_t index = features_.size();
        runCommand(std::make_unique<CreateFeatureCommand>(std::move(feature),
            std::vector<std::string>{std::string(sketchId)}, index));
        return *findFeatureForWrite(id);
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
        // 先在副本上验证边是否合法（两端存在、不重复、不成环），
        // 这样失败时仍抛 invalid_argument，且命令不会执行到一半。
        domain::DependencyGraph candidate = dependencyGraph_;
        if (!candidate.addDependency(
                std::string(featureId), std::string(dependsOnId))) {
            throw std::invalid_argument("无法添加特征依赖关系");
        }
        // 命令重做时依赖图必须仍能接受这条边；首次执行与校验结果一致。
        runCommand(std::make_unique<AddDependencyCommand>(
            std::string(featureId), std::string(dependsOnId)));
    }

    // 参数修改流程：定位对象 -> 查参数规则 -> 校验 -> 生成“修改参数命令”。
    std::vector<std::string> ModelDocument::dependenciesOf(std::string_view featureId) const
    {
        if (!findFeature(featureId)) throw std::invalid_argument("找不到依赖查询对象");
        return dependencyGraph_.dependenciesOf(std::string(featureId));
    }

    void ModelDocument::setParameter(std::string_view featureId,std::string_view parameterName,double value)
    {
        // 稳定 ID 是 UI、AI、历史恢复共同使用的对象身份。
        domain::Feature* feature = findFeatureForWrite(featureId);
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

        double previousValue = value;
        for (const auto& parameter : feature->parameters())
            if (parameter.name() == parameterName) previousValue = parameter.asDouble();
        if (previousValue == value) return; // 值没变：不产生历史。

        // 先在真实对象上试写并做跨参数校验（例如圆台两个半径不能同时为零），
        // 失败时原地恢复旧值；旧的“整文档 restoreState”重建已被命令模式取代。
        feature->setParameter(std::string(parameterName), value);
        const std::string validationError = feature->validate();
        if (!validationError.empty()) {
            feature->setParameter(std::string(parameterName), previousValue);
            throw std::invalid_argument(validationError);
        }
        // 校验通过后把对象恢复到修改前，再让命令正式执行这次修改，
        // 保证“第一次 execute”与“Redo”走完全相同的代码路径。
        feature->setParameter(std::string(parameterName), previousValue);
        runCommand(std::make_unique<ModifyParameterCommand>(
            std::string(featureId), std::string(parameterName), previousValue, value));
    }

    void ModelDocument::setPosition(std::string_view featureId, double x, double y, double z)
    {
        auto* feature=findFeatureForWrite(featureId);
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
        double oldX=0.0, oldY=0.0, oldZ=0.0;
        for (const auto& parameter : feature->parameters()) {
            if (parameter.name()=="x") oldX=parameter.asDouble();
            else if (parameter.name()=="y") oldY=parameter.asDouble();
            else if (parameter.name()=="z") oldZ=parameter.asDouble();
        }
        // 试写三个坐标并做跨参数校验，失败则原地恢复；成功也先恢复，
        // 再交给组合命令正式执行，使首次执行与 Redo 对称。
        try {
            for (const auto& [name,value] : values) feature->setParameter(name,value);
            const auto error=feature->validate();
            if (!error.empty()) throw std::invalid_argument(error);
        } catch (...) {
            feature->setParameter("x",oldX);
            feature->setParameter("y",oldY);
            feature->setParameter("z",oldZ);
            throw;
        }
        feature->setParameter("x",oldX);
        feature->setParameter("y",oldY);
        feature->setParameter("z",oldZ);
        // 一次点击定位/一次 XYZ 编辑只占一条历史：组合命令内部是三条增量修改。
        auto composite=std::make_unique<CompositeCommand>();
        for (const auto& [name,value] : values) {
            const double oldValue = name=="x" ? oldX : name=="y" ? oldY : oldZ;
            if (oldValue!=value)
                composite->add(std::make_unique<ModifyParameterCommand>(
                    std::string(featureId), name, oldValue, value));
        }
        if (!composite->empty()) runCommand(std::move(composite));
    }

    // 删除：命令在构造阶段读取级联范围/顺序/依赖，execute 时才移出对象。
    void ModelDocument::deleteFeature(std::string_view featureId)
    {
        if (!findFeature(featureId)) {
            throw std::invalid_argument(
                "找不到要删除的 Feature: " + std::string(featureId));
        }
        runCommand(std::make_unique<DeleteFeatureCommand>(
            *this, std::string(featureId)));
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
        // CommandManager 调用栈顶命令的 undo()，并交还该状态对应的 revision。
        // 空栈直接返回 false，撤销在 UI 上就是安全空操作。
        std::uint64_t restoredRevision = revision_;
        if (history_.undo(*this, restoredRevision)) {
            revision_ = restoredRevision;
        }
    }

    void ModelDocument::redo()
    {
        // Redo 重新 execute() 同一条命令，并恢复它执行后的 revision。
        std::uint64_t restoredRevision = revision_;
        if (history_.redo(*this, restoredRevision)) {
            revision_ = restoredRevision;
        }
    }

    bool ModelDocument::canUndo() const
    {
        return history_.canUndo(); // 命令栈非空才允许撤销。
    }

    bool ModelDocument::canRedo() const
    {
        return history_.canRedo(); // Redo 栈非空才允许重做。
    }

    domain::Feature* ModelDocument::findFeatureForWrite(std::string_view id)
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
            // 用 checked 解析替代 std::stoll：畸形文件的超长数字会让 stoll 抛 out_of_range，
            // 这里统一转成业务错误 invalid_argument；合法文件的解析结果保持不变。
            const auto number = checkedIdSuffix(f.id.substr(f.type.size()));
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
                    // 先用 find 检查参数名集合：缺字段的畸形文件必须统一报 invalid_argument，
                    // 不能让 at() 的 out_of_range 泄漏给调用方；size==3 但名字不对也会被拦住。
                    const auto position = f.parameters.find(name);
                    if (position == f.parameters.end())
                        throw std::invalid_argument("导入几何资源或位置参数无效");
                    const auto value = position->second;
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
        // 打开/替换文档是一条新时间线：旧命令栈里的命令持有旧文档对象，必须全部丢弃。
        history_.clear();
        revision_ = nextRevision_++;
        savedRevision_ = revision_;
    }

    void ModelDocument::swap(ModelDocument& other) noexcept
    {
        features_.swap(other.features_);
        std::swap(dependencyGraph_,other.dependencyGraph_);
        sequenceByType_.swap(other.sequenceByType_);
        history_.swap(other.history_);
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
        } while (findFeatureForWrite(candidate)); // 兼容未来导入带现成 ID 的文档。
        return candidate; // 返回已确认没有与现有 Feature 冲突的稳定 ID。
    }

    // ============================================================
    // 命令模式内部原语
    // ------------------------------------------------------------
    // 公开 API 负责“校验 + 生成命令 + 压栈”，命令负责“精确执行/撤销”。
    // 以下方法不记录历史，只做最小增量修改；命令通过 friend 访问它们。
    // ============================================================
    void ModelDocument::runCommand(std::unique_ptr<Command> command)
    {
        if (!command) throw std::logic_error("不能执行空命令");
        const std::uint64_t revisionBefore = revision_;
        // 先占一个新版本号；命令失败时 revision_ 不变，只是版本号出现空洞（无害）。
        const std::uint64_t revisionAfter = nextRevision_++;
        // record() 内部先 execute()，成功后才压栈；失败命令不会污染历史。
        history_.record(std::move(command), *this, revisionBefore, revisionAfter);
        revision_ = revisionAfter;
    }

    void ModelDocument::insertFeatureRaw(std::unique_ptr<domain::Feature> feature, std::size_t index)
    {
        if (!feature) throw std::logic_error("不能插入空特征");
        const std::string id = feature->id();
        if (findFeatureForWrite(id)) {
            throw std::logic_error("重复的特征 ID: " + id);
        }
        // 先在依赖图副本上登记节点：分配失败时文档保持原样（强异常保证）。
        domain::DependencyGraph graph = dependencyGraph_;
        graph.addNode(id);
        // index 记录的是创建时在文档中的位置；撤销/重做后仍插回同一顺序。
        const std::size_t position = std::min(index, features_.size());
        features_.insert(features_.begin() + static_cast<std::ptrdiff_t>(position),
                         std::move(feature));
        dependencyGraph_ = std::move(graph);
    }

    std::unique_ptr<domain::Feature> ModelDocument::takeFeatureRaw(
        std::string_view id, std::size_t& index)
    {
        const auto it = std::find_if(
            features_.begin(), features_.end(),
            [id](const auto& feature) { return feature->id() == id; });
        if (it == features_.end()) {
            throw std::logic_error("要撤销的特征不存在: " + std::string(id));
        }
        // 仍被下游依赖时不能单独移出，否则依赖图会指向不存在的节点。
        // LIFO 撤销保证依赖方已经先被撤销/删除；这里仍做防御性检查。
        if (!dependencyGraph_.removeNode(std::string(id))) {
            throw std::logic_error("特征仍被下游依赖，不能单独撤销: " + std::string(id));
        }
        index = static_cast<std::size_t>(it - features_.begin());
        auto feature = std::move(*it);
        features_.erase(it);
        return feature;
    }

    std::size_t ModelDocument::indexOfFeature(std::string_view id) const
    {
        const auto it = std::find_if(
            features_.begin(), features_.end(),
            [id](const auto& feature) { return feature->id() == id; });
        if (it == features_.end()) {
            throw std::logic_error("特征不存在: " + std::string(id));
        }
        return static_cast<std::size_t>(it - features_.begin());
    }

    void ModelDocument::setFeatureParameterRaw(std::string_view id,
        std::string_view parameterName, double value)
    {
        domain::Feature* feature = findFeatureForWrite(id);
        if (!feature) throw std::logic_error("特征不存在: " + std::string(id));
        // 命令负责回放已经校验过的值，不再重复做范围/跨参数校验。
        feature->setParameter(std::string(parameterName), value);
    }

    void ModelDocument::setFeatureDefinitionRaw(std::string_view id,
        domain::FeatureDefinition definition)
    {
        auto* feature = dynamic_cast<domain::AdvancedFeature*>(findFeatureForWrite(id));
        if (!feature) throw std::logic_error("特征不支持结构化定义: " + std::string(id));
        feature->setDefinition(std::move(definition));
    }

    void ModelDocument::addDependencyRaw(std::string_view featureId,
        std::string_view dependsOnId)
    {
        if (!dependencyGraph_.addDependency(
                std::string(featureId), std::string(dependsOnId))) {
            throw std::logic_error("命令无法恢复依赖关系: " + std::string(featureId)
                + " -> " + std::string(dependsOnId));
        }
    }

    bool ModelDocument::removeDependencyRaw(std::string_view featureId,
        std::string_view dependsOnId)
    {
        return dependencyGraph_.removeDependency(
            std::string(featureId), std::string(dependsOnId));
    }

    void ModelDocument::restoreState(const DocumentState& state)
    {
        // 仅用于打开文档：先在临时容器重建全部 Feature，成功后再整体替换。
        // Undo/Redo 不再走这里，而是由命令逐条回放增量修改。
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

} // namespace forge::application
