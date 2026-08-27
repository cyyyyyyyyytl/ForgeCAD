//
// Created by 15389 on 2026/8/27.
//

#ifndef FORGECAD_PARAMETER_H
#define FORGECAD_PARAMETER_H

#include <string>       // std::string
#include <variant>      // std::variant / std::get_if
#include <stdexcept>    // std::runtime_error
#include <utility>      // std::move

namespace forge::domain {   // 门牌号：forge 项目 / domain 领域层

// 参数值：一个值可能是 double / int / bool / string 之一
using ParameterValue = std::variant<double, int, bool, std::string>;

class Parameter {
public:
    // 构造函数：名字和值。std::move = 搬进来不拷贝
    Parameter(std::string name, ParameterValue value)
        : name_(std::move(name)), value_(std::move(value)) {}

    // 返回名字（const 引用：只读，不拷贝）
    const std::string& name() const { return name_; }

    // 改值
    void setValue(ParameterValue value) { value_ = std::move(value); }

    // 取数值（double 或 int 都行）；类型不对抛异常
    double asDouble() const {
        if (auto* d = std::get_if<double>(&value_)) {   // 如果是 double
            return *d;
        }
        if (auto* i = std::get_if<int>(&value_)) {      // 如果是 int
            return static_cast<double>(*i);             // 转成 double 返回
        }
        // 都不是 → 抛异常（出错了，告诉调用方原因）
        throw std::runtime_error("Parameter '" + name_ + "' 不是数值类型");
    }

private:
    std::string name_;        // 参数名，如 "length"
    ParameterValue value_;    // 参数值
};

} // namespace forge::domain

#endif //FORGECAD_PARAMETER_H
