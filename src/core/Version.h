#pragma once
#include <string>

namespace forge::core {

// ForgeCAD 的只读版本信息入口。
//
// 版本号的真实来源是顶层 CMakeLists.txt 中的 project(VERSION ...)。
// CMake 在编译时把主、次、修订版本写入预处理宏，业务代码统一通过本结构读取，
// 避免界面、日志和测试分别维护一份容易失配的字符串。
struct Version {
    // 返回适合展示和记录日志的完整版本号，例如 "0.1.0"。
    static std::string string();

    // 以下三个接口提供可参与数值比较的独立版本分量。
    static int major();  // 不兼容的大版本变更。
    static int minor();  // 向后兼容的功能版本。
    static int patch();  // 向后兼容的问题修复版本。
};

} // namespace forge::core
