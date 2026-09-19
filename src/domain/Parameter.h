#pragma once // 防止参数类型定义在同一编译单元中重复出现。

#include <stdexcept> // asDouble 遇到非数值类型时抛出 runtime_error。
#include <string>    // 参数名以及未来可能的字符串参数值。
#include <utility>   // std::move 转移字符串和 variant 内部资源。
#include <variant>   // 一个变量在同一时刻保存多种候选类型中的一种。

namespace forge::domain {

// ParameterValue 表示参数未来可能具有的几种类型。
// 当前 Box/Cylinder/Sphere 只用 double，保留 variant 是为了以后增加开关、
// 整数段数、枚举名称等参数时不用推翻 Feature 接口。
using ParameterValue = std::variant<double, int, bool, std::string>;

// Parameter 是“某一个 Feature 实例的当前参数”，例如 Box001.length=100。
// 它不同于 ParameterDescriptor：后者描述所有 Box 的 length 允许输入什么。
class Parameter {
public:
    // 构造函数接收值并移动进成员，调用方传临时对象时可以避免额外复制。
    Parameter(std::string name, ParameterValue value)
        : name_(std::move(name))   // 保存稳定参数名，例如 length。
        , value_(std::move(value)) // 保存这个 Feature 实例当前的参数值。
    {
    }

    // 返回 const 引用避免复制字符串，同时禁止调用方通过引用修改名字。
    const std::string& name() const { return name_; }
    // 用新值替换旧值；按值接收后移动可同时兼顾左值和临时值调用。
    void setValue(ParameterValue value) { value_ = std::move(value); }

    // 数值建模代码统一取 double；int 可以安全提升，其他类型明确报错。
    double asDouble() const
    {
        if (const auto* value = std::get_if<double>(&value_)) {
            // variant 当前保存 double 时直接返回该数值。
            return *value;
        }
        if (const auto* value = std::get_if<int>(&value_)) {
            // 整数也是合法数值参数，显式提升为统一的 double 返回类型。
            return static_cast<double>(*value);
        }
        // bool 和 string 没有通用的建模数值语义，明确抛错比静默转换更安全。
        throw std::runtime_error("Parameter '" + name_ + "' 不是数值类型");
    }

private:
    std::string name_;       // 稳定参数协议名，供 UI、AI 和领域层共同识别。
    ParameterValue value_;   // 当前实际值；variant 保留将来扩展非 double 参数的能力。
};

} // namespace forge::domain
