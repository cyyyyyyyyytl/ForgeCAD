#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace forge::domain { class Feature; }

namespace forge::application {

// ============================================================
// ModelDocument：参数化模型文档（模型数据的唯一所有者）
// ------------------------------------------------------------
// 为什么不能继续让 MainWindow 保存 features_：
//   · MainWindow 是 UI；AI、脚本、文件导入不应该依赖某个窗口才能改模型。
//   · 把所有权放到 Document 后，任何入口都能通过应用层操作同一份数据。
//   · vector<unique_ptr<Feature>> 明确表达“一个 Feature 只属于一个文档”。
//
// 当前按 ID 线性查找是 O(n)，足够支撑 MVP。特征规模增大后可以额外维护
// unordered_map<string, Feature*> 索引，但 vector 仍可保留历史树顺序。
// ============================================================
class ModelDocument {
public:
    ModelDocument() = default;
    // 析构放在 .cpp 定义：头文件只前向声明 Feature，销毁 unique_ptr 时才需要完整类型。
    ~ModelDocument();
    // Document 拥有 unique_ptr，复制所有权没有明确业务含义，因此显式禁止复制。
    ModelDocument(const ModelDocument&) = delete;
    ModelDocument& operator=(const ModelDocument&) = delete;

    // 调用后所有权从调用方转移到 Document；返回引用便于选中新建对象。
    domain::Feature& addFeature(std::unique_ptr<domain::Feature> feature);

    domain::Feature* findFeature(std::string_view id);
    const domain::Feature* findFeature(std::string_view id) const;

    const std::vector<std::unique_ptr<domain::Feature>>& features() const;
    // 各类型独立编号，例如 Box001、Box002、Sphere001，并跳过已存在的导入 ID。
    std::string nextFeatureId(std::string_view type);

private:
    std::vector<std::unique_ptr<domain::Feature>> features_;
    std::map<std::string, int> sequenceByType_;
};

} // namespace forge::application
