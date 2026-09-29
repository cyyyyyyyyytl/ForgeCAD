#include "assistant/ToolRegistry.h"
#include "application/ModelDocument.h"
#include "domain/AdvancedFeature.h"
#include "geometry/AdvancedModeling.h"
#include "infrastructure/DefinitionJson.h"
#include <stdexcept>
namespace forge::assistant {
namespace {
QJsonObject object(QJsonObject p,QJsonArray r={}) {return {{"type","object"},{"properties",p},{"required",r},{"additionalProperties",false}};}
QJsonObject number(double lo=-1e6,double hi=1e6) {return {{"type","number"},{"minimum",lo},{"maximum",hi}};}
QJsonObject text() {return {{"type","string"},{"minLength",1}};}
QJsonObject boolean() {return {{"type","boolean"}};}
QJsonObject array(QJsonObject items,int lo,int hi) {return {{"type","array"},{"items",items},{"minItems",lo},{"maxItems",hi}};}
QJsonObject choice(QJsonArray a) {auto s=text();s["enum"]=a;return s;}
std::string id(const QJsonObject& a,const char* key) {return a[key].toString().trimmed().toStdString();}
}
QJsonArray ToolRegistry::advancedSchemas() const {
    QJsonArray result;
    const auto add=[&](const char* name,const char* description,QJsonObject properties,QJsonArray required) {
        result.append(QJsonObject{{"type","function"},{"function",QJsonObject{{"name",name},{"description",QString::fromUtf8(description)},{"parameters",object(properties,required)}}}});
    };
    const auto xyz=object({{"x",number()},{"y",number()},{"z",number()}},{"x","y","z"});
    auto profile=QJsonObject{{"vertices",array(object({{"u",number()},{"v",number()},{"bulge",number(-10,10)}},{"u","v"}),2,256)},
        {"plane",choice({"XY","XZ","YZ"})},{"closed",boolean()},{"x",number()},{"y",number()},{"z",number()}};
    add("create_profile","创建直线/圆弧轮廓。闭合默认true，plane默认XY。UV=XY/XZ/YZ局部坐标，XYZ世界平移。顶点bulge=tan(有向圆弧角/4)，正为UV逆时针，作用于到下一顶点的边；直线省略。不得重复首点。闭合轮廓至少3点，不自交；XZ轮廓可绕Z旋转杯身。",profile,{"vertices"});
    profile["feature_id"]=text();add("set_profile","原ID整体更新轮廓，一次撤销；省略数值/平面/closed保持原值，vertices完整替换。",profile,{"feature_id","vertices"});
    auto path=QJsonObject{{"points",array(object({{"x",number()},{"y",number()},{"z",number()},{"through",xyz}},{"x","y","z"}),2,256)},
        {"closed",boolean()},{"x",number()},{"y",number()},{"z",number()}};
    add("create_path","创建3D连接路径。默认开放；每点可提供through，表示到下一点的圆弧经过点，否则直线。开放末点不得提供through。可用圆弧扫掠弯曲杯耳。",path,{"points"});
    path["feature_id"]=text();add("set_path","原ID整体更新路径，一次撤销；points完整替换，省略其他字段保持原值。",path,{"feature_id","points"});
    add("create_transform","创建依赖变换特征，支持实体/线框。绕同一pivot依次世界X/Y/Z旋转rx/ry/rz（度），之后世界XYZ平移（mm）；默认全部0。不改变源参数，重新编辑不累计。",{{"source_id",text()},{"rx",number(-3600,3600)},{"ry",number(-3600,3600)},{"rz",number(-3600,3600)},
        {"pivot_x",number()},{"pivot_y",number()},{"pivot_z",number()},{"x",number()},{"y",number()},{"z",number()}},{"source_id"});
    add("list_edges","查询有效实体的边ID、曲线、长度、世界包围盒。ID绑定当前几何，上游改变后须重新查询，不要猜边ID。",{{"feature_id",text()}},{"feature_id"});
    add("set_edge_selection","原ID更新已有Fillet/Chamfer的选边，一次撤销。指定边失效后先查其上游list_edges，再更新edge_ids；规则模式不能提供ID。半径/距离保留，可用set_parameter另行修改。",{{"feature_id",text()},{"selection",choice({"all","top","bottom","vertical","explicit"})},{"edge_ids",array(text(),1,256)}},{"feature_id","selection"});
    for (const bool chamfer:{false,true}) {
        const char* size=chamfer?"distance":"radius";
        add(chamfer?"create_chamfer":"create_fillet",chamfer?"依赖倒角：distance毫米。selection默认top，上沿/下沿/竖直线边/全部/指定。explicit须提供list_edges的edge_ids，其余模式禁止边ID。失败尝试减小距离或调整选边。":"依赖圆角：radius毫米。selection默认top（世界Z最高处整条边）；bottom/vertical/all/explicit。explicit须提供list_edges的edge_ids，其余模式禁止边ID。上游改尺寸时规则重选，指定ID失效明确失败。",{{"base_id",text()},{size,number(.001,10000)},{"selection",choice({"all","top","bottom","vertical","explicit"})},{"edge_ids",array(text(),1,256)}},{"base_id",size});
    }
    add("create_sweep","闭合平面截面沿连接路径扫掠实体。默认align_profile=true：将截面面积中心置于路径起点，并使法线对齐起始切线。false要求用户已对齐。先分析几何，过大截面或路径尖角可能失败。",{{"profile_id",text()},{"path_id",text()},{"align_profile",boolean()}},{"profile_id","path_id"});
    add("create_loft","按profile_ids顺序连接2到32个不同的闭合平面截面，生成实体。需先设置各截面平面、位置、尺寸；ruled=false默认平滑插值，true直纹面。支持变化截面杯身和立柱。",{{"profile_ids",array(text(),2,32)},{"ruled",boolean()}},{"profile_ids"});
    return result;
}
QJsonObject ToolRegistry::executeAdvanced(const QString& name,const QJsonObject& a) {
    if (name=="set_edge_selection") {
        const auto feature=id(a,"feature_id");const auto* f=document_.findFeature(feature);
        if(!f || (f->type()!="Fillet" && f->type()!="Chamfer"))throw std::invalid_argument("更新选边需要已有圆角／倒角");
        domain::NumericParameters p;for(const auto& v:f->parameters())p[v.name()]=v.asDouble();
        p["selection"]=QStringList{"all","top","bottom","vertical","explicit"}.indexOf(a["selection"].toString());
        domain::FeatureDefinition d;if(a.contains("edge_ids"))d=infrastructure::decodeDefinition({{"edge_ids",a["edge_ids"]}});
        document_.setAdvancedDefinition(feature,p,std::move(d));return changedFeature(*document_.findFeature(feature));
    }
    if (name=="list_edges") {
        const auto report=document_.rebuildReport();const auto feature=id(a,"feature_id");
        if (!report.contains(feature) || report.at(feature).status!=core::RebuildStatus::Ready) throw std::invalid_argument("选边对象不存在或重建失败");
        QJsonArray edges;
        for (const auto& e:geometry::AdvancedModeling::edges(report.at(feature).shape)) {
            QJsonArray bounds;for(double v:e.bounds)bounds.append(v);
            edges.append(QJsonObject{{"edge_id",QString::fromStdString(e.id)},{"curve",QString::fromStdString(e.curve)},{"length_mm",e.length},{"bounds_mm",bounds}});
        }
        return {{"success",true},{"feature_id",a["feature_id"]},{"edges",edges},{"bounds_order","xmin,ymin,zmin,xmax,ymax,zmax"}};
    }
    domain::NumericParameters p;domain::FeatureDefinition d;std::vector<std::string> inputs;std::string type;
    const bool edit=name=="set_profile" || name=="set_path";
    if (name=="create_profile" || name=="set_profile" || name=="create_path" || name=="set_path") {
        const bool profile=name.endsWith("profile");type=profile?"ProfileSketch":"Path3D";
        if(edit) {
            const auto* f=document_.findFeature(id(a,"feature_id"));
            if(!f || f->type()!=type)throw std::invalid_argument("编辑对象类型不匹配");
            for(const auto& v:f->parameters())p[v.name()]=v.asDouble();
        }
        for(const char* key:{"x","y","z"})if(a.contains(key))p[key]=a[key].toDouble();
        if(a.contains("closed"))p["closed"]=a["closed"].toBool()?1:0;
        if(profile) {
            if(a.contains("plane")) {const auto plane=a["plane"].toString();p["plane"]=plane=="XY"?0:plane=="XZ"?1:2;}
            d=infrastructure::decodeDefinition({{"vertices",a["vertices"]}});
        } else d=infrastructure::decodeDefinition({{"path_points",a["points"]}});
    } else if(name=="create_transform") {
        type="Transform";inputs={id(a,"source_id")};
        for(const char* key:{"rx","ry","rz","pivot_x","pivot_y","pivot_z","x","y","z"})if(a.contains(key))p[key]=a[key].toDouble();
    } else if(name=="create_fillet" || name=="create_chamfer") {
        type=name=="create_fillet"?"Fillet":"Chamfer";inputs={id(a,"base_id")};const char* size=type=="Fillet"?"radius":"distance";p[size]=a[size].toDouble();
        p["selection"]=QStringList{"all","top","bottom","vertical","explicit"}.indexOf(a["selection"].toString("top"));
        if(a.contains("edge_ids"))d=infrastructure::decodeDefinition({{"edge_ids",a["edge_ids"]}});
    } else if(name=="create_sweep") {type="Sweep";inputs={id(a,"profile_id"),id(a,"path_id")};p["align_profile"]=a["align_profile"].toBool(true)?1:0;}
    else if(name=="create_loft") {type="Loft";for(const auto& v:a["profile_ids"].toArray())inputs.push_back(v.toString().trimmed().toStdString());p["ruled"]=a["ruled"].toBool()?1:0;}
    else throw std::invalid_argument("未知高级工具");
    if(edit) {const auto feature=id(a,"feature_id");document_.setAdvancedDefinition(feature,p,std::move(d));return changedFeature(*document_.findFeature(feature));}
    return changedFeature(document_.createAdvancedFeature(type,p,std::move(d),inputs));
}
}
