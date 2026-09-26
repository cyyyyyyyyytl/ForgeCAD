#pragma once

#include "domain/Parameter.h"
#include <cmath>
#include <vector>

namespace forge::domain {

// 世界坐标系中的平移，单位毫米；限制到一公里以避免极大坐标降低几何精度。
inline constexpr double positionLimit = 1000000.0;
inline bool isPositionParameter(const std::string& name)
{
    return name == "x" || name == "y" || name == "z";
}

// 尺寸必须为正；位置允许零和负数。公共校验避免三种基本体规则分叉。
inline std::string validatePrimitiveParameters(const std::vector<Parameter>& parameters)
{
    for (const auto& parameter : parameters) {
        const double value = parameter.asDouble();
        if (!std::isfinite(value)) return "参数 " + parameter.name() + " 必须是有限数值";
        if (isPositionParameter(parameter.name())) {
            if (std::abs(value) > positionLimit) return "位置参数超出范围: " + parameter.name();
        } else if (value <= 0.0) {
            return "参数 " + parameter.name() + " 必须大于 0";
        }
    }
    return {};
}

} // namespace forge::domain
