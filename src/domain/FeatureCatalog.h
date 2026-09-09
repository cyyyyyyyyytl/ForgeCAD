#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace forge::domain {

// AI Tool Call 天然使用 {参数名: 数值}，具名参数也避免依赖 vector 下标顺序。
using NumericParameters = std::unordered_map<std::string, double>;

struct ParameterDescriptor {
    std::string name;
    double defaultValue;
    double minimum;
    double maximum;
};

struct FeatureDescriptor {
    std::string type;
    std::vector<ParameterDescriptor> parameters;
};

// ============================================================
// FeatureCatalog：可创建特征的元数据目录
// ------------------------------------------------------------
// 同一份描述同时驱动：
//   · Qt 新建对话框的控件、默认值和范围
//   · FeatureFactory 对具名参数的检查与排序
//   · AI tools 中可用 Feature 类型和参数说明
// Feature 自身仍保留 validate()，那是领域不变量的防御性校验。
// ============================================================
class FeatureCatalog {
public:
    static const std::vector<FeatureDescriptor>& all();
    static const FeatureDescriptor* find(std::string_view type);
    static const ParameterDescriptor* findParameter(std::string_view type,
                                                    std::string_view parameterName);
};

} // namespace forge::domain
