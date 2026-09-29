#include "infrastructure/DefinitionJson.h"
#include <QJsonArray>
#include <cmath>
#include <stdexcept>
namespace forge::infrastructure {
namespace {
void require(bool ok) { if (!ok) throw std::invalid_argument("轮廓、路径或选边定义无效"); }
double number(const QJsonObject& o,const char* key,double fallback=0,bool optional=false) {
    if (optional && !o.contains(key)) return fallback;
    require(o[key].isDouble() && std::isfinite(o[key].toDouble()));return o[key].toDouble();
}
void fields(const QJsonObject& o,const QStringList& allowed) { for (auto i=o.begin();i!=o.end();++i) require(allowed.contains(i.key())); }
std::array<double,3> xyz(const QJsonObject& o) { fields(o,{"x","y","z"});return {number(o,"x"),number(o,"y"),number(o,"z")}; }
QJsonObject xyz(const std::array<double,3>& p) { return {{"x",p[0]},{"y",p[1]},{"z",p[2]}}; }
}
QJsonObject encodeDefinition(const domain::FeatureDefinition& d) {
    QJsonObject o;
    if (!d.vertices.empty()) { QJsonArray a;for (const auto& p:d.vertices) a.append(QJsonObject{{"u",p.u},{"v",p.v},{"bulge",p.bulge}});o["vertices"]=a; }
    if (!d.pathPoints.empty()) { QJsonArray a;for (const auto& p:d.pathPoints) {auto point=xyz(p.point);if(p.through)point["through"]=xyz(*p.through);a.append(point);}o["path_points"]=a; }
    if (!d.edgeIds.empty()) { QJsonArray a;for(const auto& id:d.edgeIds)a.append(QString::fromStdString(id));o["edge_ids"]=a; }
    return o;
}
domain::FeatureDefinition decodeDefinition(const QJsonObject& o) {
    fields(o,{"vertices","path_points","edge_ids"});domain::FeatureDefinition d;
    for (const auto* key:{"vertices","path_points","edge_ids"}) {
        if (!o.contains(key)) continue;
        require(o[key].isArray() && o[key].toArray().size()<=256);
        for (const auto& v:o[key].toArray()) {
            if (QString(key)=="edge_ids") {require(v.isString());d.edgeIds.push_back(v.toString().toStdString());continue;}
            require(v.isObject());const auto p=v.toObject();
            if (QString(key)=="vertices") {
                fields(p,{"u","v","bulge"});d.vertices.push_back({number(p,"u"),number(p,"v"),number(p,"bulge",0,true)});
            } else {
                fields(p,{"x","y","z","through"});domain::PathPoint point;
                point.point={number(p,"x"),number(p,"y"),number(p,"z")};
                if (p.contains("through")) {require(p["through"].isObject());point.through=xyz(p["through"].toObject());}
                d.pathPoints.push_back(point);
            }
        }
    }
    return d;
}
}
