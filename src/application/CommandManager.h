#pragma once // 历史管理器被 ModelDocument 以值成员持有，只能定义一次。

#include "application/Command.h" // Entry 保存 Command 的 unique_ptr，需要完整接口。

#include <cstdint> // revisionBefore/revisionAfter 与文档保存点比较。
#include <cstddef> // std::size_t 用于历史条目计数。
#include <memory>  // unique_ptr 表达“命令栈拥有命令对象”。
#include <vector>  // 两条栈用 vector 实现，末尾即栈顶。

namespace forge::application {

class ModelDocument;

// ============================================================
// CommandManager：Undo/Redo 双栈
// ------------------------------------------------------------
// 只负责“哪条命令在栈顶”，不负责怎么改模型：
//   · record() 先执行命令，成功后才压栈；新命令会清空 Redo 分支；
//   · undo()/redo() 调用命令并交还该状态对应的 revision；
//   · 空栈时 undo/redo 返回 false，由 ModelDocument 安静地忽略。
//
// revision 是“内容版本号”的映射：每个历史条目记录执行前/后的 revision，
// Undo 回到 before、Redo 回到 after。这样“撤销回保存点”仍能被 isModified()
// 准确识别为未修改（与旧的整文档快照语义一致）。
// ============================================================
class CommandManager {
public:
    CommandManager() = default;
    // 历史栈拥有命令对象，不能复制；移动/交换用于打开文档时的事务式替换。
    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;
    CommandManager(CommandManager&&) noexcept = default;
    CommandManager& operator=(CommandManager&&) noexcept = default;

    // 执行命令并压入 Undo 栈；命令抛异常时不会入栈（文档由命令自行保证无半成品）。
    // revisionBefore/After 记录执行前后的内容版本，撤销/重做时原样交还。
    void record(std::unique_ptr<Command> command, ModelDocument& document,
                std::uint64_t revisionBefore, std::uint64_t revisionAfter);

    // 返回 false 表示对应栈为空；成功时通过 restoredRevision 交还应恢复的版本号。
    bool undo(ModelDocument& document, std::uint64_t& restoredRevision);
    bool redo(ModelDocument& document, std::uint64_t& restoredRevision);

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    std::size_t undoCount() const { return undoStack_.size(); }
    std::size_t redoCount() const { return redoStack_.size(); }

    // 打开新文档/替换文档内容时清空全部历史。
    void clear();
    void swap(CommandManager& other) noexcept;

private:
    // 一条历史 = 命令 + 内容版本号。revision 与文档的 revision_ 配合使用。
    struct Entry {
        std::unique_ptr<Command> command;
        std::uint64_t revisionBefore = 0;
        std::uint64_t revisionAfter = 0;
    };

    std::vector<Entry> undoStack_; // 末尾是最近一次可撤销操作。
    std::vector<Entry> redoStack_; // 末尾是最近一次可重做操作。
};

} // namespace forge::application
