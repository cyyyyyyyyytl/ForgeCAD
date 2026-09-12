#pragma once

#include "application/Command.h"

#include <string>

// 这里只保存 Feature 引用，因此头文件暂时不需要看到 Feature 的完整定义。
namespace forge::domain {
class Feature;
}

namespace forge::application {

// ModifyParameterCommand 把“一次参数修改”保存成可执行、可撤销的对象。
class ModifyParameterCommand final : public Command {
public:
    ModifyParameterCommand(domain::Feature& feature,
                           std::string parameterName,
                           double newValue);

    // 把参数改成新值。
    void execute() override;

    // 把参数恢复成旧值。
    void undo() override;

private:
    // Feature 仍由 ModelDocument 拥有；命令只保存对它的引用。
    domain::Feature& feature_;

    // 必须记录参数名，因为一个 Box 有 length、width、height 等多个参数。
    std::string parameterName_;

    // 创建命令时保存旧值，Undo 才知道应该恢复成什么。
    double oldValue_ = 0.0;

    // Execute 和 Redo 都会写入这个新值。
    double newValue_ = 0.0;
};

} // namespace forge::application
