#include "domain/FeatureRegistry.h"

#include "domain/BoxFeature.h"
#include "domain/CylinderFeature.h"
#include "domain/SphereFeature.h"

#include <algorithm>
#include <stdexcept>

namespace forge::domain {

const std::vector<FeatureDescriptor>& FeatureRegistry::all()
{
    // 局部 static 只初始化一次；返回 const 引用避免反复复制和外部篡改。
    // 参数排列也是下面 create() 生成 values 数组时采用的构造顺序。
    static const std::vector<FeatureDescriptor> descriptors = {
        {"Box", {
            {"length", 100.0, 1.0, 10000.0},
            {"width",   50.0, 1.0, 10000.0},
            {"height",  30.0, 1.0, 10000.0},
        }},
        {"Cylinder", {
            {"radius", 20.0, 1.0, 10000.0},
            {"height", 60.0, 1.0, 10000.0},
        }},
        {"Sphere", {
            {"radius", 20.0, 1.0, 10000.0},
        }},
    };
    return descriptors;
}

const FeatureDescriptor* FeatureRegistry::find(std::string_view type)
{
    // 当前只有三类特征，线性查找比额外索引更直接。
    const auto& descriptors = all();
    const auto it = std::find_if(
        descriptors.begin(), descriptors.end(),
        [type](const FeatureDescriptor& descriptor) {
            return descriptor.type == type;
        });
    return it == descriptors.end() ? nullptr : &*it;
}

const ParameterDescriptor* FeatureRegistry::findParameter(
    std::string_view type,
    std::string_view parameterName)
{
    // 先找到所属特征，再在它的参数表中寻找，避免不同类型参数串用。
    const FeatureDescriptor* feature = find(type);
    if (!feature) {
        return nullptr;
    }

    const auto it = std::find_if(
        feature->parameters.begin(), feature->parameters.end(),
        [parameterName](const ParameterDescriptor& parameter) {
            return parameter.name == parameterName;
        });
    return it == feature->parameters.end() ? nullptr : &*it;
}

std::unique_ptr<Feature> FeatureRegistry::create(
    const std::string& type,
    const std::string& id,
    const NumericParameters& parameters)
{
    // 第一层校验：特征类型必须已登记。
    const FeatureDescriptor* descriptor = find(type);
    if (!descriptor) {
        throw std::invalid_argument("未知特征类型: " + type);
    }
    // 数量先快速检查；下面仍会逐个检查必填名字和范围。
    if (parameters.size() != descriptor->parameters.size()) {
        throw std::invalid_argument(type + " 参数数量不正确");
    }

    // unordered_map 没有固定顺序，所以按 Descriptor 顺序整理成 values。
    std::vector<double> values;
    values.reserve(descriptor->parameters.size());
    for (const auto& parameter : descriptor->parameters) {
        const auto value = parameters.find(parameter.name);
        if (value == parameters.end()) {
            throw std::invalid_argument(type + " 缺少参数: " + parameter.name);
        }
        if (value->second < parameter.minimum ||
            value->second > parameter.maximum) {
            throw std::invalid_argument(type + " 参数超出范围: " + parameter.name);
        }
        values.push_back(value->second);
    }

    // 这里是抽象类型名到具体 C++ 类的唯一映射位置。
    if (type == "Box") {
        return std::make_unique<BoxFeature>(id, values[0], values[1], values[2]);
    }
    if (type == "Cylinder") {
        return std::make_unique<CylinderFeature>(id, values[0], values[1]);
    }
    if (type == "Sphere") {
        return std::make_unique<SphereFeature>(id, values[0]);
    }

    // 说明已登记但创建分支遗漏，这是开发错误而不是用户输入错误。
    throw std::logic_error("特征已登记但没有创建实现: " + type);
}

} // namespace forge::domain
