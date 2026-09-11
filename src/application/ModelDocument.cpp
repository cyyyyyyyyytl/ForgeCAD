#include "application/ModelDocument.h"

#include "domain/Feature.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace forge::application {

// 析构函数必须在 Feature 已经是完整类型的 .cpp 中生成。
// 这样包含 ModelDocument.h 的调用方只需要看到 Feature 的前向声明，
// 既减少头文件依赖，也确保 unique_ptr<Feature> 能正确调用虚析构函数。
ModelDocument::~ModelDocument() = default;

// ============================================================
// addFeature：把一个新特征正式加入当前文档
// ------------------------------------------------------------
// unique_ptr 按值传入表示所有权转移。进入函数后先做两层防御：
// ① 指针不能为空；② ID 不能和文档内已有特征重复。
// 两项检查都通过后才 move 进 vector，异常时原文档保持不变。
// ============================================================
domain::Feature& ModelDocument::addFeature(std::unique_ptr<domain::Feature> feature)
{
    // 空 unique_ptr 没有可管理的 Feature，加入后会让后续遍历解引用崩溃。
    if (!feature) {
        throw std::invalid_argument("不能向文档加入空 Feature");
    }

    // 稳定 ID 是 UI、AI 和未来依赖图定位对象的共同钥匙，必须保持唯一。
    if (findFeature(feature->id())) {
        throw std::invalid_argument("Feature ID 已存在: " + feature->id());
    }

    // std::move 将唯一所有权交给 vector；返回引用指向刚加入的堆对象。
    features_.push_back(std::move(feature));
    return *features_.back();
}

// 可修改查找版本：应用服务需要通过返回指针修改参数。
// 没找到时返回 nullptr，让调用方显式处理“对象不存在”，而不是抛出容器异常。
domain::Feature* ModelDocument::findFeature(std::string_view id)
{
    // 捕获 string_view 不发生字符串复制；比较时只读取调用期间有效的字符区间。
    const auto it = std::find_if(features_.begin(), features_.end(),
        [id](const auto& feature) { return feature->id() == id; });
    return it == features_.end() ? nullptr : it->get();
}

// 只读查找版本：当 Document 本身是 const 时，只允许调用 Feature 的 const 接口。
const domain::Feature* ModelDocument::findFeature(std::string_view id) const
{
    const auto it = std::find_if(features_.begin(), features_.end(),
        [id](const auto& feature) { return feature->id() == id; });
    return it == features_.end() ? nullptr : it->get();
}

// 返回 const 容器引用，避免复制 unique_ptr，也禁止调用方增删 vector 元素。
// Feature 的真正增删入口仍由 ModelDocument 方法统一控制。
const std::vector<std::unique_ptr<domain::Feature>>& ModelDocument::features() const
{
    return features_;
}

// ============================================================
// nextFeatureId：按特征类型生成文档内唯一的可读 ID
// ------------------------------------------------------------
// 每个类型拥有独立计数器：Box001、Box002 与 Sphere001 互不影响。
// setw(3)+setfill('0') 负责补齐三位编号；do/while 会跳过导入文件中
// 可能已经存在的同名 ID，因此计数器状态和文档内容不一致时也安全。
// ============================================================
std::string ModelDocument::nextFeatureId(std::string_view type)
{
    // map 的 key 必须拥有字符串内容，所以把非拥有型 string_view 转成 string。
    const std::string typeName(type);

    // operator[] 在类型第一次出现时创建值为 0 的计数器，并返回其引用。
    auto& sequence = sequenceByType_[typeName];
    std::string candidate;
    do {
        // stringstream 组合类型名和补零序号，例如 Box + 001。
        std::ostringstream stream;
        stream << typeName << std::setw(3) << std::setfill('0') << ++sequence;
        candidate = stream.str();
        // 若导入文档已经有这个 ID，则继续递增直到找到空位。
    } while (findFeature(candidate));
    return candidate;
}

} // namespace forge::application
