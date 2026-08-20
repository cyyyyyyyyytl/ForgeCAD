#pragma once
#include <string>

namespace forge::core {

// ForgeCAD 版本信息
struct Version {
    static std::string string();      // "0.1.0"
    static int major();
    static int minor();
    static int patch();
};

} // namespace forge::core
