#include "core/Version.h"
#include <spdlog/spdlog.h>

namespace forge::core {

int Version::major() { return FORGECAD_VERSION_MAJOR; }
int Version::minor() { return FORGECAD_VERSION_MINOR; }
int Version::patch() { return FORGECAD_VERSION_PATCH; }

std::string Version::string() {
    spdlog::debug("Version::string() called");
    return FORGECAD_VERSION_STRING;
}

} // namespace forge::core
