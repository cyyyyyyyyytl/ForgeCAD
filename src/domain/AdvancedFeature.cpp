#include "domain/AdvancedFeature.h"
#include "geometry/AdvancedModeling.h"
#include "geometry/ShapeFactory.h"
#include <Standard_Failure.hxx>
#include <cmath>
#include <set>
#include <stdexcept>

namespace forge::domain {
bool AdvancedFeature::consumesInputs(std::string_view t) {
    return t=="Transform" || t=="Fillet" || t=="Chamfer" || t=="Sweep" || t=="Loft";
}
bool AdvancedFeature::isType(std::string_view t) {
    return consumesInputs(t) || t=="Cone" || t=="ProfileSketch" || t=="Path3D";
}
AdvancedFeature::AdvancedFeature(std::string id,std::string type,const NumericParameters& values,FeatureDefinition definition)
    :Feature(std::move(id),std::move(type)),definition_(std::move(definition)) {
    const auto* descriptor=FeatureRegistry::find(this->type());
    if (!descriptor || !isType(this->type())) throw std::invalid_argument("未知高级特征");
    for (const auto& [name,v]:values)
        if (!FeatureRegistry::findParameter(this->type(),name)) throw std::invalid_argument("未知参数: "+name);
    for (const auto& p:descriptor->parameters) {
        const auto it=values.find(p.name);
        if (it==values.end() && !p.optional) throw std::invalid_argument("缺少参数: "+p.name);
        parameters_.emplace_back(p.name,it==values.end()?p.defaultValue:it->second);
    }
    const auto error=validate(); if (!error.empty()) throw std::invalid_argument(error);
}
double AdvancedFeature::value(const char* name) const {
    for (const auto& p:parameters_) if (p.name()==name) return p.asDouble();
    throw std::logic_error("缺少特征参数");
}
void AdvancedFeature::setParameter(const std::string& name,ParameterValue v) {
    for (auto& p:parameters_) if (p.name()==name) { p.setValue(std::move(v));return; }
    throw std::invalid_argument("未知参数: "+name);
}
std::string AdvancedFeature::validate() const {
    for (const auto& p:parameters_) {
        const auto* d=FeatureRegistry::findParameter(type(),p.name()); const double v=p.asDouble();
        if (!d || !std::isfinite(v) || v<d->minimum || v>d->maximum) return "参数超出范围: "+p.name();
        if ((p.name()=="plane" || p.name()=="selection" || p.name()=="closed" || p.name()=="align_profile" || p.name()=="ruled") && std::floor(v)!=v)
            return "枚举参数必须是整数: "+p.name();
    }
    const auto finite=[](double v){ return std::isfinite(v) && std::abs(v)<=1e6; };
    if (type()=="Cone" && value("radius_bottom")==0 && value("radius_top")==0) return "两个半径不能同时为零";
    if (type()=="ProfileSketch") {
        const auto n=definition_.vertices.size();
        if (n<(value("closed")?3u:2u) || n>256) return "轮廓顶点数量无效（最多256）";
        for (const auto& v:definition_.vertices)
            if (!finite(v.u) || !finite(v.v) || !std::isfinite(v.bulge) || std::abs(v.bulge)>10) return "轮廓坐标或圆弧 bulge 无效";
        if (!value("closed") && definition_.vertices.back().bulge!=0) return "开放轮廓末点不能带圆弧";
    } else if (!definition_.vertices.empty()) return "此特征不接受轮廓顶点";
    if (type()=="Path3D") {
        const auto n=definition_.pathPoints.size();
        if (n<(value("closed")?3u:2u) || n>256) return "路径点数量无效（最多256）";
        for (const auto& p:definition_.pathPoints) {
            for (double v:p.point) if (!finite(v)) return "路径坐标无效";
            if (p.through) for (double v:*p.through) if (!finite(v)) return "圆弧经过点无效";
        }
        if (!value("closed") && definition_.pathPoints.back().through) return "开放路径末点不能带圆弧";
    } else if (!definition_.pathPoints.empty()) return "此特征不接受路径点";
    if (type()=="Fillet" || type()=="Chamfer") {
        if ((value("selection")==4)!=(!definition_.edgeIds.empty())) return "指定边模式须提供边ID，规则选边不能提供边ID";
        std::set<std::string> unique;
        if (definition_.edgeIds.size()>256) return "选边过多";
        for (const auto& id:definition_.edgeIds) if (id.empty() || id.size()>64 || !unique.insert(id).second) return "边ID重复或无效";
    } else if (!definition_.edgeIds.empty()) return "此特征不接受边ID";
    return {};
}
TopoDS_Shape AdvancedFeature::rebuild(const std::vector<TopoDS_Shape>& inputs) const { return rebuildResult(inputs).shape; }
core::ShapeResult AdvancedFeature::rebuildResult(const std::vector<TopoDS_Shape>& inputs) const {
    try {
        const auto error=validate(); if (!error.empty()) throw std::invalid_argument(error);
        const auto require=[&](std::size_t n){ if (inputs.size()!=n) throw std::invalid_argument("输入数量错误"); };
        TopoDS_Shape shape;
        using G=geometry::AdvancedModeling;
        if (type()=="Cone") { require(0);shape=G::cone(value("radius_bottom"),value("radius_top"),value("height")); }
        else if (type()=="ProfileSketch") { require(0);shape=G::profile(definition_.vertices,value("closed")!=0,static_cast<int>(value("plane"))); }
        else if (type()=="Path3D") { require(0);shape=G::path(definition_.pathPoints,value("closed")!=0); }
        else if (type()=="Transform") {
            require(1);
            if (!geometry::ShapeFactory::isSolidBody(inputs[0]) && (inputs[0].IsNull() || inputs[0].ShapeType()!=TopAbs_WIRE)) throw std::invalid_argument("变换需要实体或线框");
            shape=G::transform(inputs[0],{value("rx"),value("ry"),value("rz")},{value("pivot_x"),value("pivot_y"),value("pivot_z")},{value("x"),value("y"),value("z")});
        } else if (type()=="Fillet" || type()=="Chamfer") {
            require(1);shape=G::dressEdges(inputs[0],type()=="Chamfer",value(type()=="Fillet"?"radius":"distance"),static_cast<int>(value("selection")),definition_.edgeIds);
        } else if (type()=="Sweep") { require(2);shape=G::sweep(inputs[0],inputs[1],value("align_profile")!=0); }
        else if (type()=="Loft") {
            if (inputs.size()<2 || inputs.size()>32) throw std::invalid_argument("放样需要2到32个有序截面");
            shape=G::loft(inputs,value("ruled")!=0);
        }
        if (type()=="Cone" || type()=="ProfileSketch" || type()=="Path3D") shape=geometry::ShapeFactory::translate(shape,value("x"),value("y"),value("z"));
        return geometry::ShapeFactory::inspectShape(shape);
    } catch (const Standard_Failure& e) { return {{},core::RebuildStatus::Failed,e.GetMessageString()?e.GetMessageString():"OCCT建模失败"}; }
    catch (const std::exception& e) { return {{},core::RebuildStatus::Failed,e.what()}; }
}
}
