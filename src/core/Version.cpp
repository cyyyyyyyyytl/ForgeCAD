#include "core/Version.h" // Version 静态接口声明。

#include <spdlog/spdlog.h> // 记录版本字符串被读取的调试信息。

namespace forge::core {

// 三个整数由根 CMakeLists.txt 中的 project(VERSION ...) 编译定义注入，
// 业务代码无需重复维护版本号，也不会出现界面版本和构建版本不一致。
int Version::major() { return FORGECAD_VERSION_MAJOR; } // 返回 CMake 注入的主版本整数。
int Version::minor() { return FORGECAD_VERSION_MINOR; } // 返回 CMake 注入的次版本整数。
int Version::patch() { return FORGECAD_VERSION_PATCH; } // 返回 CMake 注入的修订版本整数。

// 返回完整的语义化版本字符串，例如 "0.1.0"。
// debug 日志仅用于追踪版本查询，不影响 Release 下的功能逻辑。
std::string Version::string()
{
    spdlog::debug("Version::string() called"); // 仅调试级别记录，不改变返回结果。
    return FORGECAD_VERSION_STRING; // 宏已包含类似 0.1.0 的完整编译期字符串。
}

} // namespace forge::core
