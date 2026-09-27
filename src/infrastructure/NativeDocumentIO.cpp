#include "infrastructure/NativeDocumentIO.h"
#include "infrastructure/DocumentJson.h"
#include "infrastructure/ZipArchive.h"
#include "application/ModelDocument.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

namespace forge::infrastructure {
namespace {
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
}
void NativeDocumentIO::save(const application::DocumentData& data,const QString& path) {
    check(!path.trimmed().isEmpty(),"保存路径为空");
    application::ModelDocument validation; validation.replaceData(data);
    ZipEntries entries;
    entries["manifest.json"] = QJsonDocument(QJsonObject{{"format","ForgeCAD"},{"version",1},
        {"document","document.json"}}).toJson();
    entries["document.json"] = DocumentJson::encode(data);
    for (const auto& [name,bytes] : data.geometryAssets) {
        check(bytes.size()<=size_t(ZipArchive::maximumBytes),"几何资源过大");
        entries.emplace(QByteArray::fromStdString(name),QByteArray::fromStdString(bytes));
    }
    const auto bytes=ZipArchive::encode(entries);
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
        throw std::runtime_error(("保存失败："+file.errorString()).toStdString());
}
application::DocumentData NativeDocumentIO::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(("打开失败："+file.errorString()).toStdString());
    check(file.size()<=ZipArchive::maximumBytes,"文档文件超过 64 MiB");
    const auto bytes=file.read(ZipArchive::maximumBytes+1);
    check(file.error()==QFileDevice::NoError,"文件读取失败");
    const auto entries=ZipArchive::decode(bytes);
    check(entries.contains("manifest.json") && entries.contains("document.json"),"文档缺少 manifest.json 或 document.json");
    QJsonParseError error;
    const auto manifest=QJsonDocument::fromJson(entries.at("manifest.json"),&error);
    check(error.error==QJsonParseError::NoError && manifest.isObject(),"文档清单 JSON 无效");
    const auto m=manifest.object();
    check(m["format"]=="ForgeCAD" && m["version"].isDouble() && m["version"].toDouble()==1 &&
          m["document"]=="document.json","文档格式或版本不支持");
    check(!m.contains("zipProfile") || m["zipProfile"]=="stored-v1","未知旧文档 ZIP 配置");
    check(!m.contains("requiredCapabilities"),"文档要求尚未支持的能力");
    auto data=DocumentJson::decode(entries.at("document.json"));
    for (const auto& [name,payload] : entries)
        if (name!="manifest.json" && name!="document.json") {
            check(name.startsWith("shapes/") && name.endsWith(".brep"),"文档含未知资源");
            data.geometryAssets.emplace(name.toStdString(),payload.toStdString());
        }
    // 解码与恢复分开；prepare 只恢复一次，提交用文档交换。
    return data;
}
std::unique_ptr<application::ModelDocument> NativeDocumentIO::prepare(const QString& path) {
    auto document=std::make_unique<application::ModelDocument>();
    document->replaceData(load(path));
    return document;
}
}