#pragma once

#include "application/Command.h"

#include <memory>
#include <vector>

namespace forge::application {

class CommandManager {
public:
    // 接收并执行一个新命令。
    void executeCommand(std::unique_ptr<Command> command);

    // 撤销最近执行的命令。
    void undo();

    // 重做最近撤销的命令。
    void redo();

    bool canUndo() const;
    bool canRedo() const;

private:
    // vector 的末尾充当栈顶。
    std::vector<std::unique_ptr<Command>> undoStack_;

    // 保存已经撤销、可以重新执行的命令。
    std::vector<std::unique_ptr<Command>> redoStack_;
};

} // namespace forge::application
