#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace forge::domain {

// ParameterValue 表示参数未来可能具有的几种类型。
// 当前 Box/Cylinder/Sphere 只用 double，保留 variant 是为了以后增加开关、
// 整数段数、枚举名称等参数时不用推翻 Feature 接口。
using ParameterValue = std::variant<double, int, bool, std::string>;

// Parameter 是“某一个 Feature 实例的当前参数”，例如 Box001.length=100。
// 它不同于 ParameterDescriptor：后者描述所有 Box 的 length 允许输入什么。
class Parameter {
public:
    Parameter(std::string name, ParameterValue value)
        : name_(std::move(name)), value_(std::move(value)) {}

    const std::string& name() const { return name_; }
    void setValue(ParameterValue value) { value_ = std::move(value); }

    // 数值建模代码统一取 double；int 可以安全提升，其他类型明确报错。
    double asDouble() const
    {
        if (const auto* value = std::get_if<double>(&value_)) {
            return *value;
        }
        if (const auto* value = std::get_if<int>(&value_)) {
            return static_cast<double>(*value);
        }
        throw std::runtime_error("Parameter '" + name_ + "' 不是数值类型");
    }

private:
    std::string name_;
    ParameterValue value_;
};

} // namespace forge::domain
