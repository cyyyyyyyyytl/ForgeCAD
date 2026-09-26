#include "domain/FeatureRegistry.h" // Descriptor、NumericParameters 和 Registry 声明。

#include "domain/BoxFeature.h"      // Box 分支需要完整类型才能 make_unique。
#include "domain/CylinderFeature.h" // Cylinder 分支需要完整类型。
#include "domain/SphereFeature.h"   // Sphere 分支需要完整类型。

#include "domain/PositionParameters.h"

#include <algorithm> // std::find_if 负责按名称查找类型和参数。
#include <stdexcept> // 区分用户输入错误 invalid_argument 和开发遗漏 logic_error。

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
            {"x", 0.0, -positionLimit, positionLimit, true},
            {"y", 0.0, -positionLimit, positionLimit, true},
            {"z", 0.0, -positionLimit, positionLimit, true},
        }},
        {"Cylinder", {
            {"radius", 20.0, 1.0, 10000.0},
            {"height", 60.0, 1.0, 10000.0},
            {"x", 0.0, -positionLimit, positionLimit, true},
            {"y", 0.0, -positionLimit, positionLimit, true},
            {"z", 0.0, -positionLimit, positionLimit, true},
        }},
        {"Sphere", {
            {"radius", 20.0, 1.0, 10000.0},
            {"x", 0.0, -positionLimit, positionLimit, true},
            {"y", 0.0, -positionLimit, positionLimit, true},
            {"z", 0.0, -positionLimit, positionLimit, true},
        }},
    };
    return descriptors; // 返回同一份静态登记表的只读引用，不发生容器复制。
}

const FeatureDescriptor* FeatureRegistry::find(std::string_view type)
{
    // 导入类型只能由实际几何创建，不加入基本体创建列表或 AI 的 create_feature 枚举。
    static const FeatureDescriptor imported{"Imported", {
        {"x", 0.0, -positionLimit, positionLimit, true},
        {"y", 0.0, -positionLimit, positionLimit, true},
        {"z", 0.0, -positionLimit, positionLimit, true}}};
    if (type == "Imported") return &imported;
    // 当前只有三类特征，线性查找比额外索引更直接。
    const auto& descriptors = all(); // 借用全局唯一登记表，避免复制全部描述。
    const auto it = std::find_if(
        descriptors.begin(), descriptors.end(),
        [type](const FeatureDescriptor& descriptor) {
            return descriptor.type == type;
        });
    // end 表示没找到；否则 *it 是对象，&*it 取得登记表中该对象的地址。
    return it == descriptors.end() ? nullptr : &*it;
}

const ParameterDescriptor* FeatureRegistry::findParameter(
    std::string_view type,
    std::string_view parameterName)
{
    // 先找到所属特征，再在它的参数表中寻找，避免不同类型参数串用。
    const FeatureDescriptor* feature = find(type);
    if (!feature) {
        return nullptr; // 连类型都不存在，自然不可能存在它的参数说明。
    }

    const auto it = std::find_if(
        feature->parameters.begin(), feature->parameters.end(),
        [parameterName](const ParameterDescriptor& parameter) {
            return parameter.name == parameterName;
        });
    // 返回登记表内部参数对象的只读地址，调用方不能修改公共规则。
    return it == feature->parameters.end() ? nullptr : &*it;
}

std::unique_ptr<Feature> FeatureRegistry::create(
    const std::string& type,
    const std::string& id,
    const NumericParameters& parameters)
{
    if (type == "Imported") throw std::invalid_argument("导入特征必须提供原始几何");
    // 第一层校验：特征类型必须已登记。
    const FeatureDescriptor* descriptor = find(type);
    if (!descriptor) {
        throw std::invalid_argument("未知特征类型: " + type);
    }
    // 拒绝未知字段；尺寸仍必填，三个位置分量可独立省略。
    for (const auto& [name, value] : parameters) {
        if (!findParameter(type, name)) throw std::invalid_argument(type + " 没有参数: " + name);
    }

    // unordered_map 没有固定顺序，所以按 Descriptor 顺序整理成 values。
    std::vector<double> values;
    values.reserve(descriptor->parameters.size()); // 预留准确容量，避免循环中扩容。
    for (const auto& parameter : descriptor->parameters) {
        const auto value = parameters.find(parameter.name);
        if (value == parameters.end()) {
            if (!parameter.optional) throw std::invalid_argument(type + " 缺少参数: " + parameter.name);
            values.push_back(parameter.defaultValue);
            continue;
        }
        if (!std::isfinite(value->second) || value->second < parameter.minimum ||
            value->second > parameter.maximum) {
            throw std::invalid_argument(type + " 参数超出范围: " + parameter.name);
        }
        // 按 Descriptor 的稳定顺序放入数组，供具体特征构造函数安全使用。
        values.push_back(value->second);
    }

    // 这里是抽象类型名到具体 C++ 类的唯一映射位置。
    if (type == "Box") {
        return std::make_unique<BoxFeature>(id, values[0], values[1], values[2], values[3], values[4], values[5]);
    }
    if (type == "Cylinder") {
        return std::make_unique<CylinderFeature>(id, values[0], values[1], values[2], values[3], values[4]);
    }
    if (type == "Sphere") {
        return std::make_unique<SphereFeature>(id, values[0], values[1], values[2], values[3]);
    }

    // 说明已登记但创建分支遗漏，这是开发错误而不是用户输入错误。
    throw std::logic_error("特征已登记但没有创建实现: " + type);
}

} // namespace forge::domain
