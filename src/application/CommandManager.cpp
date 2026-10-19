#include "application/CommandManager.h"

#include <stdexcept>
#include <utility>

namespace forge::application {

void CommandManager::record(std::unique_ptr<Command> command, ModelDocument& document,
    std::uint64_t revisionBefore, std::uint64_t revisionAfter)
{
    if (!command) {
        throw std::logic_error("不能记录空命令");
    }
    // 先执行再压栈：命令失败时直接抛出，历史栈保持原样。
    // 首次执行与 Redo 共用同一个 execute()，因此命令自身必须可重入。
    command->execute(document);
    // 新操作代表一条新的时间线，之前撤销出来的 Redo 分支全部失效。
    // 丢弃 redoStack_ 时会析构其中的命令对象（例如被撤销的创建命令持有的 Feature）。
    redoStack_.clear();
    undoStack_.push_back(Entry{std::move(command), revisionBefore, revisionAfter});
}

bool CommandManager::undo(ModelDocument& document, std::uint64_t& restoredRevision)
{
    if (undoStack_.empty()) {
        return false;
    }
    // 先调用命令、成功后再移动条目：撤销失败时历史栈不会被破坏。
    Entry& entry = undoStack_.back();
    entry.command->undo(document);
    restoredRevision = entry.revisionBefore;
    redoStack_.push_back(std::move(entry));
    undoStack_.pop_back();
    return true;
}

bool CommandManager::redo(ModelDocument& document, std::uint64_t& restoredRevision)
{
    if (redoStack_.empty()) {
        return false;
    }
    Entry& entry = redoStack_.back();
    entry.command->execute(document);
    restoredRevision = entry.revisionAfter;
    undoStack_.push_back(std::move(entry));
    redoStack_.pop_back();
    return true;
}

void CommandManager::clear()
{
    undoStack_.clear();
    redoStack_.clear();
}

void CommandManager::swap(CommandManager& other) noexcept
{
    undoStack_.swap(other.undoStack_);
    redoStack_.swap(other.redoStack_);
}

} // namespace forge::application
