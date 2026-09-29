#include "assistant/ToolRegistry.h"
#include "application/ModelDocument.h"
#include "domain/Feature.h"
#include "domain/AdvancedFeature.h"
#include "infrastructure/DefinitionJson.h"
#include "domain/FeatureRegistry.h"
#include "domain/BooleanFeature.h"
#include "geometry/ShapeAnalyzer.h"
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <Standard_Failure.hxx>
#include <cmath>
#include <stdexcept>

namespace forge::assistant {
namespace {
QJsonObject objectSchema(const QJsonObject& properties, const QJsonArray& required = {})
{
    return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};
}
QJsonObject functionTool(const QString& name, const QString& description, const QJsonObject& parameters)
{
    return {{"type","function"},{"function",QJsonObject{{"name",name},{"description",description},{"parameters",parameters}}}};
}
QJsonObject textProperty(const QString& description)
{
    return {{"type","string"},{"description",description},{"minLength",1}};
}
QJsonObject enumProperty(const QJsonArray& values, const QString& description)
{
    auto result=textProperty(description); result.insert("enum",values); return result;
}
QJsonObject numberProperty(double minimum, double maximum, const QString& description)
{
    return {{"type","number"},{"minimum",minimum},{"maximum",maximum},{"description",description}};
}
QString requireString(const QJsonObject& object, const QString& key)
{
    const auto value=object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty())
        throw std::invalid_argument(("缺少字符串参数: "+key).toStdString());
    return value.toString().trimmed();
}
domain::ExtrudeDirection directionOf(const QJsonObject& arguments)
{
    const auto direction=arguments.value("direction").toString("forward");
    if (direction=="reverse") return domain::ExtrudeDirection::Reverse;
    if (direction=="symmetric") return domain::ExtrudeDirection::Symmetric;
    return domain::ExtrudeDirection::Forward;
}
bool supportsPosition(std::string_view type)
{
    return domain::FeatureRegistry::findParameter(type,"x") &&
        domain::FeatureRegistry::findParameter(type,"y") &&
        domain::FeatureRegistry::findParameter(type,"z");
}
QJsonObject spatialRules()
{
    return {{"success",true},{"units",QJsonObject{{"length","mm"},{"angle","degree"}}},
        {"coordinate_system","右手世界 XYZ；示例以 Z 为竖直方向。相机旋转不改变世界坐标。"},
        {"primitives",QJsonObject{{"Box","XYZ 是基准角点，实体沿 +X/+Y/+Z 延伸。"},
            {"Cylinder","XYZ 是底面圆心，轴沿 +Z，顶面 z=底面z+height。"},
            {"Sphere","XYZ 是球心，半径 radius；最低/最高 z=球心z±radius。"}}},
        {"sketches","plane=0:XY(U=X,V=Y)，1:XZ(U=X,V=Z)，2:YZ(U=Y,V=Z)；XYZ是世界平移，矩形从UV原点开始，圆以UV原点为圆心；ProfileSketch顶点是局部UV坐标。"},
        {"extrude","沿草图规范法线：XY:+Z，XZ:-Y，YZ:+X；reverse取反，symmetric两侧各height/2。通用Transform可变换结果。"},
        {"position_supported_types",QJsonArray{"Box","Cylinder","Sphere","Cone","RectangleSketch","CircleSketch","ProfileSketch","Path3D","Transform","Imported","Revolve"}},
        {"position_unsupported_types",QJsonArray{"Extrude","ExtrudeCut","Cut","Union","Intersection"}},
        {"position_note","Imported/Revolve/Transform的XYZ为世界平移，设置值替换原值。create_transform支持实体/线框，绕同一pivot依次世界X/Y/Z旋转（度），之后XYZ平移。基础体、轮廓和路径的XYZ是世界基准平移。"},
        {"advanced","Cone底面圆心XYZ、轴+Z，radius_bottom/radius_top任一可0但不能同时0。ProfileSketch圆弧bulge=tan(有向角/4)，正为UV逆时针。Sweep默认将截面面积中心置于路径起点并对齐法线与切线。Loft输入是有序闭合截面列表。Fillet/Chamfer规则按世界Z上下沿/竖直线边，或list_edges查询指定边ID；几何改变后指定ID失效报错。"},
        {"revolve",QJsonObject{
            {"axis_origin",QJsonArray{0,0,0}},{"axes",QJsonArray{"X","Y","Z"}},
            {"plane_rule","X接受XY/XZ；Y接受XY/YZ；Z接受XZ/YZ。XZ轮廓绕Z可生成竖直杯身。轴经过世界原点，生成后可平移。"},
            {"X_point_rotation","(x,y,z)->(x, y*cosθ-z*sinθ, y*sinθ+z*cosθ)；x 不变，y/z 旋转。"},
            {"Y_point_rotation","(x,y,z)->(x*cosθ+z*sinθ, y, -x*sinθ+z*cosθ)；y 不变，x/z 旋转。"},
            {"circle_examples",QJsonArray{
                QJsonObject{{"sketch_center",QJsonArray{10,20,0}},{"radius",2},{"axis","Y"},{"angle",360},
                    {"ring_center",QJsonArray{0,20,0}},{"center_circle_plane","XZ"},{"major_radius",10},{"tube_radius",2}},
                QJsonObject{{"sketch_center",QJsonArray{25,10,0}},{"radius",2},{"axis","X"},{"angle",360},
                    {"ring_center",QJsonArray{25,0,0}},{"center_circle_plane","YZ"},{"major_radius",10},{"tube_radius",2}}}},
            {"translated_ring_example",QJsonObject{{"sketch_center",QJsonArray{10,0,0}},{"radius",2},{"axis","Y"},{"angle",180},
                {"result_translation",QJsonArray{35,0,120}},{"ring_center",QJsonArray{35,0,120}},
                {"steps","先 create_revolve，再 set_position 到生成的 Revolve ID；180度生成半环，先 analyze_geometry 核对方向与位置，再与杯身布尔合并。整圈用360度。"}}},
            {"notes","示例为 z=0 的圆形轮廓，管直径=2*草图radius。圆环中心可沿旋转轴偏移，并非必须在原点；改变垂直轴的分量不是平移成品。非零草图z会改变扫掠几何，不能用来抬高环心。矩形也能旋转生成实体，旋转不只做圆环；不得跨过当前径向边界。"}}},
        {"connection_check","包围盒重叠只是候选关系，不能证明实体相交。点/线接触不等于可靠实体连接；并集 ready 或一个 Union ID 不代表只有一个实体，用 analyze_geometry 查询 solid_count。single_solid 也不证明壁厚、强度或审美合格。"}};
}
QJsonObject analysisToJson(const geometry::ShapeAnalysis& data)
{
    const auto point=[](double x,double y,double z) -> QJsonObject { return {{"x",x},{"y",y},{"z",z}}; };
    QJsonValue bounds=QJsonValue::Null, centroid=QJsonValue::Null;
    if (data.bounds) {
        const auto& b=*data.bounds;
        bounds=QJsonObject{{"min",point(b[0],b[1],b[2])},{"max",point(b[3],b[4],b[5])},
            {"size",point(b[3]-b[0],b[4]-b[1],b[5]-b[2])}};
    }
    if (data.volumeCentroid) {
        const auto& p=*data.volumeCentroid; centroid=point(p[0],p[1],p[2]);
    }
    QJsonArray warnings;
    if (data.empty) warnings.append("结果为空，没有实体或可测尺寸。");
    else if (!data.solidBody) warnings.append("包含非实体几何（例如草图）；体积与体积质心不可用。");
    if (data.solids>1) warnings.append("结果含多个实体，不能声称已合并成单一实体；若目标是一体模型，请检查连接位置并修正。");
    if (data.solidBody && data.volume && *data.volume<=0) warnings.append("实体的有向体积非正，请检查拓扑方向；不能据此判断可靠实体。");
    return {{"valid",true},{"empty",data.empty},{"solid_body",data.solidBody},
        {"solid_count",data.solids},{"single_solid",data.solidBody && data.solids==1},
        {"topology",QJsonObject{{"faces",data.faces},{"edges",data.edges},{"vertices",data.vertices}}},
        {"bounds_mm",bounds},{"volume_mm3",data.volume ? QJsonValue(*data.volume) : QJsonValue(QJsonValue::Null)},
        {"surface_area_mm2",data.area},{"volume_centroid_mm",centroid},{"warnings",warnings}};
}
// Schema 和执行共用校验，未知字段、枚举和非有限值不会被默默忽略。
void validateValue(const QJsonValue& value, const QJsonObject& schema, const QString& path)
{
    const auto fail=[&]() { throw std::invalid_argument(("工具参数无效: "+path).toStdString()); };
    const auto type=schema.value("type").toString();
    if (type=="object") {
        if (!value.isObject()) fail();
        const auto object=value.toObject();
        const auto properties=schema.value("properties").toObject();
        for (const auto& required : schema.value("required").toArray())
            if (!object.contains(required.toString())) fail();
        for (auto it=object.begin();it!=object.end();++it) {
            if (properties.contains(it.key())) validateValue(it.value(),properties.value(it.key()).toObject(),path+"."+it.key());
            else if (schema.value("additionalProperties").isObject())
                validateValue(it.value(),schema.value("additionalProperties").toObject(),path+"."+it.key());
            else fail();
        }
    } else if (type=="array") {
        if (!value.isArray()) fail();const auto items=value.toArray();
        if (schema.contains("minItems") && items.size()<schema["minItems"].toInt()) fail();
        if (schema.contains("maxItems") && items.size()>schema["maxItems"].toInt()) fail();
        for (qsizetype i=0;i<items.size();++i) validateValue(items[i],schema["items"].toObject(),path+"["+QString::number(i)+"]");
    } else if (type=="boolean") {
        if (!value.isBool()) fail();
    } else if (type=="string") {
        if (!value.isString() || value.toString().trimmed().isEmpty()) fail();
    } else if (type=="number") {
        if (!value.isDouble() || !std::isfinite(value.toDouble())) fail();
        if (schema.contains("minimum") && value.toDouble()<schema.value("minimum").toDouble()) fail();
        if (schema.contains("maximum") && value.toDouble()>schema.value("maximum").toDouble()) fail();
    }
    if (schema.contains("enum") && !schema.value("enum").toArray().contains(value)) fail();
}
}

ToolRegistry::ToolRegistry(application::ModelDocument& document) : document_(document) {}

QJsonArray ToolRegistry::schemas() const
{
    QJsonArray types;
    for (const auto& descriptor : domain::FeatureRegistry::all()) types.append(QString::fromStdString(descriptor.type));
    const auto id=textProperty("稳定 Feature ID；先用 list_features 查询，勿猜测编号。");
    const auto path=textProperty("本机文件路径；省略时打开文件选择框，不要猜测用户路径。");
    const auto heightRule=domain::FeatureRegistry::findParameter("Extrude","height");
    const auto height=numberProperty(heightRule->minimum,heightRule->maximum,"拉伸总高度，毫米。对称时两侧各一半。");
    const auto direction=enumProperty({"forward","reverse","symmetric"},"沿草图法线的方向，默认 forward；正向、反向、对称。");
    const auto angleRule=domain::FeatureRegistry::findParameter("Revolve","angle");
    const auto angle=numberProperty(angleRule->minimum,angleRule->maximum,"旋转角度，度；360 为整圈。");
    QJsonArray result{
        functionTool("list_features","列出全部特征、参数、输入依赖和重建状态。",objectSchema({})),
        functionTool("get_feature","查询特征参数、输入依赖和重建状态。",objectSchema({{"feature_id",id}},{"feature_id"})),
        functionTool("create_feature","创建 Box、Cylinder、Sphere、Cone、RectangleSketch 或 CircleSketch。草图plane:0=XY,1=XZ,2=YZ。毫米；Box 的位置是基准角点，Cylinder 是底面圆心，Sphere 是球心，草图位置是矩形起点/圆心。先用 get_feature_types 查询规则。",
            objectSchema({{"type",enumProperty(types,"独立基础体或草图类型。")},
                {"parameters",QJsonObject{{"type","object"},{"description","Box:length/width/height；Cylinder:radius/height；Sphere:radius；Cone:radius_bottom/radius_top/height；RectangleSketch:length/width；CircleSketch:radius。可选 x/y/z 默认0，允许负值。"},{"additionalProperties",QJsonObject{{"type","number"}}}}}},{"type","parameters"})),
        functionTool("set_parameter","修改一个数值参数并联动重建。angle 单位度；axis:0=X,1=Y,2=Z；plane:0=XY,1=XZ,2=YZ；direction:0=正向,1=反向,2=对称。仅数据成功不代表下游几何成功。",objectSchema({{"feature_id",id},{"parameter_name",textProperty("登记的参数名，可先查询 get_feature_types。")},{"value",QJsonObject{{"type","number"}}}},{"feature_id","parameter_name","value"})),
        functionTool("delete_feature","删除特征及所有下游依赖；先弹本机确认框，取消后不得重试。",objectSchema({{"feature_id",id}},{"feature_id"})),
        functionTool("get_feature_types","查询全部特征的参数范围、默认值、枚举含义、定位支持和空间规则。",objectSchema({})),
        functionTool("get_document_status","查询特征数量、未保存状态、撤销重做状态及失败特征。",objectSchema({})),
        functionTool("set_position","支持 Box/Cylinder/Sphere/RectangleSketch/CircleSketch/Cone/ProfileSketch/Path3D/Transform/Imported/Revolve；拉伸、切除、布尔结果不支持。一次设置XYZ记一次撤销。基础体/草图为世界基准位置；Imported为原始几何平移；Revolve为旋转完成后的世界平移量，替换原值而非累计，环心=原始环心+XYZ，毫米。",objectSchema({{"feature_id",id},{"x",numberProperty(-1000000,1000000,"X 位置或附加平移")},{"y",numberProperty(-1000000,1000000,"Y 位置或附加平移")},{"z",numberProperty(-1000000,1000000,"Z 位置或附加平移")}},{"feature_id","x","y","z"})),
        functionTool("create_extrude","由一个现有闭合平面草图创建独立拉伸实体。",objectSchema({{"sketch_id",id},{"height",height},{"direction",direction}},{"sketch_id","height"})),
        functionTool("create_extrude_cut","用草图沿法线拉伸切除主体；盲孔/通孔由位置、方向和高度决定。",objectSchema({{"base_id",id},{"sketch_id",id},{"height",height},{"direction",direction}},{"base_id","sketch_id","height"})),
        functionTool("create_revolve","闭合轮廓绕世界原点 X/Y/Z 轴生成独立实体，之后可用 set_position 平移结果到杯身侧面等位置。规划前查 get_spatial_rules。圆半径2、圆心(10,20,0)、Y轴360度：原始环心(0,20,0)，中心圆平行XZ；X轴则平行YZ。管直径=2*radius，180度半圈；禁止轮廓跨轴。",objectSchema({{"sketch_id",id},{"angle",angle},{"axis",enumProperty({"X","Y","Z"},"世界轴，默认 Y；不支持任意轴、旋转切除或对称旋转。")}},{"sketch_id","angle"})),
        functionTool("create_boolean","两个不同特征的差集、并集、交集。difference 为主体 base 减去工具 tool。",objectSchema({{"operation",enumProperty({"difference","union","intersection"},"布尔操作")},{"base_id",id},{"tool_id",id}},{"operation","base_id","tool_id"})),
        functionTool("undo","撤销最后一次模型修改。没有历史则明确返回未执行。",objectSchema({})),
        functionTool("redo","重做最后一次已撤销的模型修改。",objectSchema({})),
        functionTool("new_document","新建空文档；未保存修改会提示保存/丢弃/取消。",objectSchema({})),
        functionTool("open_document","打开 .forgecad；未保存提示后完整读取，失败保留当前文档。",objectSchema({{"path",path}})),
        functionTool("save_document","保存当前原生文档到已有路径；未命名文档弹保存框。",objectSchema({})),
        functionTool("save_document_as","另存为 .forgecad；可给 path，省略则选文件；覆盖需确认。",objectSchema({{"path",path}})),
        functionTool("import_step","导入 STEP 并追加为 Imported 特征，支持撤销；不恢复源参数历史。",objectSchema({{"path",path}})),
        functionTool("export_step","导出最终有效几何；失败/阻塞拒绝导出；不修改模型历史。",objectSchema({{"path",path}})),
        functionTool("select_feature","选中特征，联动树、属性面板和三维高亮。",objectSchema({{"feature_id",id}},{"feature_id"})),
        functionTool("begin_point_placement","启动选定草图/基础体的点击定位；返回等待用户点击，尚未修改 XYZ。",objectSchema({{"feature_id",id}},{"feature_id"})),
        functionTool("cancel_point_placement","取消尚未完成的点击定位，不修改文档。",objectSchema({})),
        functionTool("control_view","适配全部模型、缩放、平移或旋转相机；不会旋转模型实体，不记入撤销。",objectSchema({{"operation",enumProperty({"fit","zoom","pan","rotate"},"视图操作")},{"factor",numberProperty(0.01,100,"zoom 必填，>1 放大，<1 缩小")},{"dx",numberProperty(-10000,10000,"pan/rotate 必填，水平像素增量")},{"dy",numberProperty(-10000,10000,"pan/rotate 必填，垂直像素增量（向下为正）")}},{"operation"})),
        functionTool("get_spatial_rules","查询世界坐标、基础体基准、旋转公式与例子、可定位类型和连接判断限制。设计关键结构前查询，勿反复猜测。",objectSchema({})),
        functionTool("analyze_geometry","只读分析精确几何：世界包围盒/XYZ尺寸、体积、面积、体积质心和实体数量；不修改历史。可指定 feature_id，省略则只分析当前文档最终结果，不重复计算被消费的上游。任何失败阻止整文档分析。多个实体不能声称单一实体；不验证壁厚、强度或审美。",objectSchema({{"feature_id",id}}))
    };
    for (const auto& schema:advancedSchemas()) result.append(schema);
    return result;
}

void ToolRegistry::validateArguments(const QString& toolName, const QJsonObject& arguments) const
{
    for (const auto& entry : schemas()) {
        const auto function=entry.toObject().value("function").toObject();
        if (function.value("name").toString()!=toolName) continue;
        validateValue(arguments,function.value("parameters").toObject(),toolName);
        if (toolName=="control_view") {
            const auto op=arguments.value("operation").toString();
            const bool valid=op=="fit" ? arguments.size()==1 : op=="zoom" ? arguments.size()==2 && arguments.contains("factor") : arguments.size()==3 && arguments.contains("dx") && arguments.contains("dy");
            if (!valid) throw std::invalid_argument("视图操作参数不匹配");
        }
        return;
    }
    throw std::invalid_argument(("未知工具: "+toolName).toStdString());
}

QJsonObject ToolRegistry::execute(const QString& toolName, const QJsonObject& arguments)
{
    try {
        validateArguments(toolName,arguments);
        for (const auto& schema:advancedSchemas())
            if (schema.toObject()["function"].toObject()["name"].toString()==toolName) return executeAdvanced(toolName,arguments);
        if (toolName=="list_features") return listFeatures();
        if (toolName=="get_feature") return getFeature(arguments);
        if (toolName=="create_feature") return createFeature(arguments);
        if (toolName=="set_parameter") return setParameter(arguments);
        if (toolName=="delete_feature") return deleteFeature(arguments);
        if (toolName=="get_feature_types") return featureTypes();
        if (toolName=="get_spatial_rules") return spatialRules();
        if (toolName=="analyze_geometry") return analyzeGeometry(arguments);
        if (toolName=="get_document_status") {
            auto result=documentStatus();
            if (uiToolHandler_) result.insert("ui",uiToolHandler_(toolName,arguments));
            return result;
        }
        if (toolName=="set_position") {
            const auto id=requireString(arguments,"feature_id").toStdString();
            document_.setPosition(id,arguments.value("x").toDouble(),arguments.value("y").toDouble(),arguments.value("z").toDouble());
            return changedFeature(*document_.findFeature(id));
        }
        if (toolName=="create_extrude") return changedFeature(document_.createExtrudeFeature(requireString(arguments,"sketch_id").toStdString(),arguments.value("height").toDouble(),directionOf(arguments)));
        if (toolName=="create_extrude_cut") return changedFeature(document_.createExtrudeCutFeature(requireString(arguments,"base_id").toStdString(),requireString(arguments,"sketch_id").toStdString(),arguments.value("height").toDouble(),directionOf(arguments)));
        if (toolName=="create_revolve") return changedFeature(document_.createRevolveFeature(requireString(arguments,"sketch_id").toStdString(),arguments.value("angle").toDouble(),arguments.value("axis").toString("Y")=="X" ? domain::RevolveAxis::X : arguments.value("axis").toString("Y")=="Z" ? domain::RevolveAxis::Z : domain::RevolveAxis::Y));
        if (toolName=="create_boolean") {
            const auto operation=arguments.value("operation").toString();
            const auto kind=operation=="difference" ? domain::BooleanOperation::Difference : operation=="union" ? domain::BooleanOperation::Union : domain::BooleanOperation::Intersection;
            return changedFeature(document_.createBooleanFeature(kind,requireString(arguments,"base_id").toStdString(),requireString(arguments,"tool_id").toStdString()));
        }
        if (toolName=="undo" || toolName=="redo") {
            if (!(toolName=="undo" ? document_.canUndo() : document_.canRedo()))
                return {{"success",false},{"error",toolName=="undo" ? "没有可撤销的操作" : "没有可重做的操作"}};
            if (toolName=="undo") document_.undo(); else document_.redo();
            auto result=documentStatus(); result.insert("model_changed",true); return result;
        }
        if (!uiToolHandler_) return {{"success",false},{"error","此工具需要主窗口上下文"}};
        return uiToolHandler_(toolName,arguments);
    } catch (const Standard_Failure& error) {
        return {{"success",false},{"error",QString::fromUtf8(error.GetMessageString() ? error.GetMessageString() : "几何内核异常")}};
    } catch (const std::exception& error) {
        return {{"success",false},{"error",QString::fromUtf8(error.what())}};
    }
}

QJsonObject ToolRegistry::featureToJson(const domain::Feature& feature, const core::ShapeResult* knownResult) const
{
    QJsonObject parameters;
    for (const auto& parameter : feature.parameters()) parameters.insert(QString::fromStdString(parameter.name()),parameter.asDouble());
    const auto result=knownResult ? *knownResult : document_.rebuildReport().at(feature.id());
    const char* state="failed";
    switch (result.status) {
    case core::RebuildStatus::Ready: state="ready"; break;
    case core::RebuildStatus::Empty: state="empty"; break;
    case core::RebuildStatus::Failed: state="failed"; break;
    case core::RebuildStatus::Blocked: state="blocked"; break;
    }
    QJsonArray dependencies;
    // 输入按原始顺序返回，布尔的主体/工具不会颠倒。
    for (const auto& id : document_.dependenciesOf(feature.id())) dependencies.append(QString::fromStdString(id));
    const auto* advanced=dynamic_cast<const domain::AdvancedFeature*>(&feature);
    return {{"definition",advanced ? infrastructure::encodeDefinition(advanced->definition()):QJsonObject{}},{"feature_id",QString::fromStdString(feature.id())},{"type",QString::fromStdString(feature.type())},{"parameters",parameters},{"dependencies",dependencies},{"supports_position",supportsPosition(feature.type())},{"rebuild_status",state},{"rebuild_message",QString::fromStdString(result.message)}};
}
QJsonObject ToolRegistry::documentStatus() const
{
    QJsonArray failures;
    const auto report=document_.rebuildReport();
    for (const auto& feature : document_.features())
        if (!report.at(feature->id()).usable()) failures.append(featureToJson(*feature,&report.at(feature->id())));
    return {{"success",true},{"feature_count",static_cast<qint64>(document_.features().size())},{"modified",document_.isModified()},{"can_undo",document_.canUndo()},{"can_redo",document_.canRedo()},{"failed_features",failures}};
}
QJsonObject ToolRegistry::changedFeature(const domain::Feature& feature) const
{
    auto result=featureToJson(feature); result.insert("success",true); result.insert("model_changed",true);
    result.insert("document_status",documentStatus()); return result;
}
QJsonObject ToolRegistry::featureTypes() const
{
    QJsonArray types;
    const auto add=[&](const domain::FeatureDescriptor& descriptor,const QString& tool) {
        QJsonArray parameters;
        for (const auto& p : descriptor.parameters) {
            QJsonObject rule{{"name",QString::fromStdString(p.name)},{"minimum",p.minimum},{"maximum",p.maximum},{"default",p.defaultValue},{"optional",p.optional}};
            rule.insert("unit",(p.name=="angle" || p.name=="rx" || p.name=="ry" || p.name=="rz") ? "degree" : (p.name=="axis" || p.name=="direction" || p.name=="plane" || p.name=="selection" || p.name=="closed" || p.name=="ruled" || p.name=="align_profile") ? "enum" : "mm");
            if (p.name=="plane") rule.insert("values",QJsonObject{{"XY",0},{"XZ",1},{"YZ",2}});
            if (p.name=="selection") rule.insert("values",QJsonObject{{"all",0},{"top",1},{"bottom",2},{"vertical",3},{"explicit",4}});
            if (p.name=="axis") rule.insert("values",QJsonObject{{"X",0},{"Y",1},{"Z",2}});
            if (p.name=="direction") rule.insert("values",QJsonObject{{"forward",0},{"reverse",1},{"symmetric",2}});
            parameters.append(rule);
        }
        types.append(QJsonObject{{"type",QString::fromStdString(descriptor.type)},{"creation_tool",tool},{"supports_position",supportsPosition(descriptor.type)},{"parameters",parameters}});
    };
    for (const auto& descriptor : domain::FeatureRegistry::all()) add(descriptor,"create_feature");
    for (const auto& pair : {std::pair{"Extrude","create_extrude"},std::pair{"ExtrudeCut","create_extrude_cut"},std::pair{"Revolve","create_revolve"},std::pair{"Imported","import_step"}}) add(*domain::FeatureRegistry::find(pair.first),pair.second);
    for (const auto& pair:{std::pair{"ProfileSketch","create_profile"},std::pair{"Path3D","create_path"},std::pair{"Transform","create_transform"},std::pair{"Fillet","create_fillet"},std::pair{"Chamfer","create_chamfer"},std::pair{"Sweep","create_sweep"},std::pair{"Loft","create_loft"}}) add(*domain::FeatureRegistry::find(pair.first),pair.second);
    for (const char* type : {"Cut","Union","Intersection"}) types.append(QJsonObject{{"type",type},{"creation_tool","create_boolean"},{"supports_position",false},{"parameters",QJsonArray{}}});
    return {{"success",true},{"types",types},{"spatial_rules",spatialRules()}};
}
QJsonObject ToolRegistry::analyzeGeometry(const QJsonObject& arguments) const
{
    const auto report=document_.rebuildReport();
    QJsonArray ids;
    TopoDS_Shape shape;
    if (arguments.contains("feature_id")) {
        const auto id=requireString(arguments,"feature_id");
        const auto* feature=document_.findFeature(id.toStdString());
        if (!feature) throw std::invalid_argument(("找不到 Feature: "+id).toStdString());
        const auto& result=report.at(feature->id());
        if (!result.usable()) {
            auto failure=featureToJson(*feature,&result);
            failure.insert("success",false); failure.insert("error","无法分析失败或阻塞的几何"); return failure;
        }
        shape=result.shape; ids.append(id);
    } else {
        QJsonArray failures;
        for (const auto& feature : document_.features()) {
            const auto& result=report.at(feature->id());
            if (!result.usable()) failures.append(featureToJson(*feature,&result));
        }
        if (!failures.isEmpty()) return {{"success",false},{"error","文档含失败或阻塞特征；不把显示回退的上游当成最终结果。"},{"failed_features",failures}};
        TopoDS_Compound compound; BRep_Builder builder; builder.MakeCompound(compound);
        for (const auto& id : document_.visibleFeatureIds(report)) {
            ids.append(QString::fromStdString(id));
            const auto& result=report.at(id);
            if (result.status!=core::RebuildStatus::Empty) builder.Add(compound,result.shape);
        }
        shape=compound;
    }
    return {{"success",true},{"scope",arguments.contains("feature_id") ? "feature" : "final_document"},
        {"feature_ids",ids},{"geometry",analysisToJson(geometry::ShapeAnalyzer::analyze(shape))},
        {"limitations","实体数量按唯一拓扑实体计数。包围盒重叠不证明连接；单实体不验证壁厚、强度、点位包含或审美。体积为空表示含非实体几何，不能当作0。"}};
}
QJsonObject ToolRegistry::listFeatures() const
{
    QJsonArray features; const auto report=document_.rebuildReport();
    for (const auto& feature : document_.features()) features.append(featureToJson(*feature,&report.at(feature->id())));
    return {{"success",true},{"features",features}};
}
QJsonObject ToolRegistry::getFeature(const QJsonObject& arguments) const
{
    const auto id=requireString(arguments,"feature_id"); const auto* feature=document_.findFeature(id.toStdString());
    if (!feature) throw std::invalid_argument(("找不到 Feature: "+id).toStdString());
    auto result=featureToJson(*feature); result.insert("success",true); return result;
}
QJsonObject ToolRegistry::createFeature(const QJsonObject& arguments)
{
    domain::NumericParameters parameters; const auto object=arguments.value("parameters").toObject();
    for (auto it=object.begin();it!=object.end();++it) parameters.emplace(it.key().toStdString(),it.value().toDouble());
    return changedFeature(document_.createFeature(requireString(arguments,"type").toStdString(),parameters));
}
QJsonObject ToolRegistry::setParameter(const QJsonObject& arguments)
{
    const auto id=requireString(arguments,"feature_id").toStdString();
    document_.setParameter(id,requireString(arguments,"parameter_name").toStdString(),arguments.value("value").toDouble());
    return changedFeature(*document_.findFeature(id));
}
QJsonObject ToolRegistry::deleteFeature(const QJsonObject& arguments)
{
    const auto id=requireString(arguments,"feature_id"); QJsonArray deleted;
    for (const auto& affected : document_.deletionOrder(id.toStdString())) deleted.append(QString::fromStdString(affected));
    document_.deleteFeature(id.toStdString());
    auto result=documentStatus(); result.insert("model_changed",true); result.insert("feature_id",id); result.insert("deleted_feature_ids",deleted); return result;
}
} // namespace forge::assistant
