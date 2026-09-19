#pragma once // Registry 是跨 UI、AI、Document 共享的头文件，只能定义一次。

#include <memory>        // create() 用 unique_ptr 返回新对象的唯一所有权。
#include <string>        // 保存类型名、参数名和 Feature ID。
#include <string_view>   // 查找接口只借用字符串，不产生额外复制。
#include <unordered_map> // NumericParameters 保存“参数名 -> 数值”。
#include <vector>        // 保持类型列表和参数显示顺序。

namespace forge::domain {

class Feature; // 返回值只需要声明类名，完整定义留给 .cpp 包含。

// 外部统一使用“参数名 -> 数值”，例如 {"radius": 20}。
// unordered_map 的顺序不重要，Registry 会按描述中的顺序调用具体构造函数。
using NumericParameters = std::unordered_map<std::string, double>;

// 一个参数的公共说明。它描述“允许怎样输入”，不保存某个对象的当前值。
struct ParameterDescriptor {
    std::string name;     // 稳定协议名，同时供 UI 和 AI 使用。
    double defaultValue;  // 新建对话框的初始值。
    double minimum;       // 允许输入的闭区间下限。
    double maximum;       // 允许输入的闭区间上限。
};

// 一种 Feature 的公共说明，例如 Box 以及它的 length/width/height。
struct FeatureDescriptor {
    std::string type;                            // 稳定类型名，例如 Box。
    std::vector<ParameterDescriptor> parameters; // 该类型全部参数的顺序和规则。
};

// ============================================================
// FeatureRegistry：支持哪些特征，以及怎样创建它们
// ------------------------------------------------------------
// Descriptor 是“产品说明书”，Feature 是“按说明书创建出来的实际对象”。
// MainWindow 用说明书生成输入框，AI 用它生成工具 Schema，ModelDocument
// 则调用 create() 创建对象。增加新类型时集中修改这一处，避免三套名单失同步。
// ============================================================
class FeatureRegistry {
public:
    // 返回全部登记项；静态数据贯穿程序生命周期，调用方只能读取。
    static const std::vector<FeatureDescriptor>& all();
    // 未知类型或参数返回 nullptr，由调用方决定怎样报告错误。
    static const FeatureDescriptor* find(std::string_view type);
    static const ParameterDescriptor* findParameter(
        std::string_view type,
        std::string_view parameterName);

    // 校验具名参数并创建具体 Feature；unique_ptr 把所有权交给调用方。
    static std::unique_ptr<Feature> create(
        const std::string& type,
        const std::string& id,
        const NumericParameters& parameters);
};

} // namespace forge::domain
