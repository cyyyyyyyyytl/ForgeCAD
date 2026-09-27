#include "infrastructure/DocumentJson.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <cmath>
#include <stdexcept>

namespace forge::infrastructure {
namespace {
void require(bool ok) { if (!ok) throw std::invalid_argument("原生文档 JSON 字段无效或版本不支持"); }
QString text(const QJsonObject& o, const char* key) {
    require(o[key].isString()); return o[key].toString();
}
int integer(const QJsonValue& v) {
    require(v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= 0 &&
            v.toDouble() <= 1000000000 && std::floor(v.toDouble()) == v.toDouble());
    return static_cast<int>(v.toDouble());
}
}
QByteArray DocumentJson::encode(const application::DocumentData& data) {
    QJsonObject root{{"format","ForgeCAD"},{"version",data.version},
        {"documentId",QString::fromStdString(data.documentId)},{"units",QString::fromStdString(data.units)}};
    QJsonObject sequences;
    for (const auto& [type,n] : data.sequences) sequences[QString::fromStdString(type)] = n;
    root["sequences"] = sequences;
    QJsonArray features;
    for (const auto& f : data.features) {
        QJsonObject item{{"id",QString::fromStdString(f.id)},{"type",QString::fromStdString(f.type)}, {"version",f.version}};
        QJsonObject parameters;
        for (const auto& [name,value] : f.parameters) {
            require(std::isfinite(value)); parameters[QString::fromStdString(name)] = value;
        }
        item["parameters"] = parameters;
        QJsonArray dependencies;
        for (const auto& id : f.dependencies) dependencies.append(QString::fromStdString(id));
        item["dependencies"] = dependencies;
        if (!f.geometryAsset.empty()) {
            item["geometryAsset"] = QString::fromStdString(f.geometryAsset);
            item["sourceName"] = QString::fromStdString(f.sourceName);
        }
        features.append(item);
    }
    root["features"] = features;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
application::DocumentData DocumentJson::decode(const QByteArray& bytes) {
    require(bytes.size() <= 16 * 1024 * 1024);
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes,&error);
    require(error.error == QJsonParseError::NoError && json.isObject());
    const auto root = json.object();
    application::DocumentData data;
    require(text(root,"format") == "ForgeCAD" && integer(root["version"]) == 1);
    data.documentId = text(root,"documentId").toStdString();
    require(!QUuid(QString::fromStdString(data.documentId)).isNull());
    require(text(root,"units") == "mm");
    require(root["sequences"].isObject() && root["features"].isArray());
    const auto sequences = root["sequences"].toObject();
    for (auto it = sequences.begin(); it != sequences.end(); ++it)
        data.sequences.emplace(it.key().toStdString(),integer(it.value()));
    const auto features = root["features"].toArray();
    require(features.size() <= 10000);
    for (const auto& value : features) {
        require(value.isObject()); const auto item = value.toObject();
        application::FeatureData f;
        f.id = text(item,"id").toStdString(); f.type = text(item,"type").toStdString();
        require(integer(item["version"]) == 1);
        require(item["parameters"].isObject() && item["dependencies"].isArray());
        const auto parameters = item["parameters"].toObject();
        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            require(it.value().isDouble() && std::isfinite(it.value().toDouble()));
            f.parameters.emplace(it.key().toStdString(),it.value().toDouble());
        }
        for (const auto& id : item["dependencies"].toArray()) {
            require(id.isString()); f.dependencies.push_back(id.toString().toStdString());
        }
        if (item.contains("geometryAsset")) {
            f.geometryAsset = text(item,"geometryAsset").toStdString();
            f.sourceName = text(item,"sourceName").toStdString();
        }
        data.features.push_back(std::move(f));
    }
    return data;
}
}
