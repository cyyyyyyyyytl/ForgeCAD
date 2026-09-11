#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace forge::domain {

// AI Tool Call 天然使用 {参数名: 数值}，具名参数也避免依赖 vector 下标顺序。
using NumericParameters = std::unordered_map<std::string, double>;

struct ParameterDescriptor {
    std::string name;     // 语言无关的协议名，例如 length；UI 可另行翻译。
    double defaultValue;  // 新建对话框和模板未指定时使用的推荐值。
    double minimum;       // 对外入口允许的最小值。
    double maximum;       // 对外入口允许的最大值。
};

struct FeatureDescriptor {
    std::string type;                            // Feature 类型名，例如 Box。
    std::vector<ParameterDescriptor> parameters; // 有序参数表，同时定义构造顺序。
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
    static const std::vector<FeatureDescriptor>& all(); // 获取全部只读登记项。
    static const FeatureDescriptor* find(std::string_view type); // 按类型查找。
    // 在指定 Feature 类型内部按参数名查找；任一级不存在都返回 nullptr。
    static const ParameterDescriptor* findParameter(std::string_view type,
                                                    std::string_view parameterName);
};

} // namespace forge::domain
