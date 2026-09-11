#include "domain/FeatureCatalog.h"

#include <algorithm>

namespace forge::domain {

// ============================================================
// all：返回程序支持的全部 Feature Schema
// ------------------------------------------------------------
// 局部 static 只在第一次调用时初始化一次，C++11 起初始化过程线程安全。
// 返回 const 引用既避免每次复制，也保证调用方不能在运行时篡改登记规则。
// 每个参数依次记录：英文名、默认值、最小值、最大值。
// ============================================================
const std::vector<FeatureDescriptor>& FeatureCatalog::all()
{
    // 参数排列顺序也是旧 Factory 构造函数所需的顺序，不可随意调换。
    static const std::vector<FeatureDescriptor> descriptors = {
        // Box 构造函数参数顺序：length、width、height。
        {"Box", {
            {"length", 100.0, 1.0, 10000.0},
            {"width",   50.0, 1.0, 10000.0},
            {"height",  30.0, 1.0, 10000.0},
        }},
        // Cylinder 构造函数参数顺序：radius、height。
        {"Cylinder", {
            {"radius", 20.0, 1.0, 10000.0},
            {"height", 60.0, 1.0, 10000.0},
        }},
        // Sphere 只有 radius 一个数值参数。
        {"Sphere", {
            {"radius", 20.0, 1.0, 10000.0},
        }},
    };
    return descriptors;
}

// 按 Feature 类型名查找 Schema。返回指向 static vector 元素的指针，
// 因为 vector 生命周期贯穿整个进程，所以该指针不会因函数返回而悬空。
const FeatureDescriptor* FeatureCatalog::find(std::string_view type)
{
    // std::find_if 让查找条件集中在 lambda 中；当前类型少，线性查找足够清晰。
    const auto& descriptors = all();
    const auto it = std::find_if(descriptors.begin(), descriptors.end(),
        [type](const FeatureDescriptor& descriptor) {
            return descriptor.type == type;
        });
    // 用 nullptr 表达未知类型，调用方据此拒绝 AI 幻觉出来的 Feature 名称。
    return it == descriptors.end() ? nullptr : &*it;
}

// 先定位 Feature Schema，再在它的参数表中定位具体参数。
// 该函数同时被属性面板和 ModelingService 使用，防止各自维护参数名单。
const ParameterDescriptor* FeatureCatalog::findParameter(
    std::string_view type,
    std::string_view parameterName)
{
    const FeatureDescriptor* feature = find(type);
    // Feature 类型本身不存在时，自然也不存在其参数。
    if (!feature) return nullptr;

    // 参数名严格区分，例如 Box 的 width 不能误写成 radius。
    const auto it = std::find_if(feature->parameters.begin(), feature->parameters.end(),
        [parameterName](const ParameterDescriptor& parameter) {
            return parameter.name == parameterName;
        });
    // 返回 Catalog 内部稳定对象的只读指针，不复制完整描述。
    return it == feature->parameters.end() ? nullptr : &*it;
}

} // namespace forge::domain
