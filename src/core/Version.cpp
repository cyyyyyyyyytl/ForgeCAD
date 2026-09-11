#include "core/Version.h"
#include <spdlog/spdlog.h>

namespace forge::core {

// 三个整数由根 CMakeLists.txt 中的 project(VERSION ...) 编译定义注入，
// 业务代码无需重复维护版本号，也不会出现界面版本和构建版本不一致。
int Version::major() { return FORGECAD_VERSION_MAJOR; }
int Version::minor() { return FORGECAD_VERSION_MINOR; }
int Version::patch() { return FORGECAD_VERSION_PATCH; }

// 返回完整的语义化版本字符串，例如 "0.1.0"。
// debug 日志仅用于追踪版本查询，不影响 Release 下的功能逻辑。
std::string Version::string() {
    spdlog::debug("Version::string() called");
    return FORGECAD_VERSION_STRING;
}

} // namespace forge::core
