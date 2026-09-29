#pragma once
#include "domain/FeatureRegistry.h"
#include "domain/FeatureDefinition.h"
#include <map>
#include <string>
#include <vector>

namespace forge::application {

    struct FeatureData {
        std::string id;                   // 稳定 ID，例如 Box001。
        std::string type;                 // Box、Cut、Imported 等。
        int version = 1;                 // 该特征的数据版本。

        domain::NumericParameters parameters;
        domain::FeatureDefinition definition;
        std::vector<std::string> dependencies; // 布尔顺序：主体、工具。

        std::string geometryAsset;        // 导入对象的几何资源名；基本体留空。
        std::string sourceName;           // 来源文件名，仅用于显示。
    };

    struct DocumentData {
        int version = 1;
        std::string documentId;           // 文档持久身份，后面用 UUID 生成。
        std::string units = "mm";

        std::vector<FeatureData> features;
        std::map<std::string, int> sequences; // 已分配序号，避免重复 ID。

        // 资源名 -> OCCT 编码后的 BRep 文本，不保存内存指针。
        std::map<std::string, std::string> geometryAssets;
    };

} // namespace forge::application