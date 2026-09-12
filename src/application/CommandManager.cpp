#include "application/CommandManager.h"

#include <stdexcept>

namespace forge::application {

bool CommandManager::canUndo() const
{
    return !undoStack_.empty();
}

bool CommandManager::canRedo() const
{
    return !redoStack_.empty();
}

void CommandManager::executeCommand(std::unique_ptr<Command> command)
{
    // 空指针无法执行，也不应该进入历史记录。
    if (!command) {
        throw std::invalid_argument("不能执行空命令");
    }

    // 先执行。若 execute() 抛出异常，命令不会进入撤销栈。
    command->execute();

    // 命令执行成功后，将所有权从局部变量转移到撤销栈。
    undoStack_.push_back(std::move(command));

    // 执行了新操作，原来的重做路线失效。
    redoStack_.clear();
}

void CommandManager::undo()
{
    // 没有历史时，什么也不做。
    if (undoStack_.empty()) {
        return;
    }

    // 从撤销栈顶取回命令的所有权。
    auto command = std::move(undoStack_.back());

    // 移除已经变成空 unique_ptr 的栈顶元素。
    undoStack_.pop_back();

    // 执行命令自己的反向操作。
    command->undo();

    // 撤销后的命令进入重做栈。
    redoStack_.push_back(std::move(command));
}

void CommandManager::redo()
{
    // Redo 盒子没有记录卡，就什么也不做。
    if (redoStack_.empty()) {
        return;
    }

    // 从 Redo 盒子拿出最上面的记录卡。
    auto command = std::move(redoStack_.back());

    // 去掉拿走记录卡后留下的空位置。
    redoStack_.pop_back();

    // 按记录卡重新执行操作。
    command->execute();

    // 做完以后，记录卡回到 Undo 盒子。
    undoStack_.push_back(std::move(command));
}

} // namespace forge::application
