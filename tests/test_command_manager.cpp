#include "application/CommandManager.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace {

// SetValueCommand 是专门用于教学测试的“假命令”。
// 它不依赖 Qt、OCCT 或真实 Feature，只把一个整数从旧值改成新值，
// 让我们能清楚观察 execute()、undo() 和 redo() 是否按预期工作。
class SetValueCommand final : public forge::application::Command {
public:
    SetValueCommand(int& target, int newValue)
        // target_ 使用引用指向外部整数；oldValue_ 在命令创建时保存原值，
        // newValue_ 保存用户希望写入的新值。
        : target_(target), oldValue_(target), newValue_(newValue)
    {
    }

    // execute() 代表“向前执行”：把目标改成新值。
    void execute() override
    {
        target_ = newValue_;
    }

    // undo() 代表“向后撤销”：把目标恢复为命令创建时记录的旧值。
    void undo() override
    {
        target_ = oldValue_;
    }

private:
    int& target_;       // 被命令修改的外部整数。
    int oldValue_;      // 撤销时需要恢复的旧值。
    int newValue_;      // 执行和重做时写入的新值。
};

TEST(CommandManagerTest, ExecuteUndoRedoMovesCommandBetweenHistories)
{
    int value = 50;
    forge::application::CommandManager manager;

    // 初始时两个历史都为空。
    EXPECT_FALSE(manager.canUndo());
    EXPECT_FALSE(manager.canRedo());

    // 执行命令：数值从 50 变成 100，命令进入 Undo 栈。
    manager.executeCommand(std::make_unique<SetValueCommand>(value, 100));
    EXPECT_EQ(value, 100);
    EXPECT_TRUE(manager.canUndo());
    EXPECT_FALSE(manager.canRedo());

    // 撤销命令：数值恢复成 50，命令从 Undo 栈移动到 Redo 栈。
    manager.undo();
    EXPECT_EQ(value, 50);
    EXPECT_FALSE(manager.canUndo());
    EXPECT_TRUE(manager.canRedo());

    // 重做命令：数值重新变成 100，命令从 Redo 栈移回 Undo 栈。
    manager.redo();
    EXPECT_EQ(value, 100);
    EXPECT_TRUE(manager.canUndo());
    EXPECT_FALSE(manager.canRedo());
}

TEST(CommandManagerTest, NewCommandClearsRedoHistory)
{
    int value = 50;
    forge::application::CommandManager manager;

    // 先执行 50 -> 100，再撤销回 50，此时旧命令位于 Redo 栈。
    manager.executeCommand(std::make_unique<SetValueCommand>(value, 100));
    manager.undo();
    ASSERT_EQ(value, 50);
    ASSERT_TRUE(manager.canRedo());

    // 撤销后执行全新的 50 -> 80，历史产生分叉，旧的 Redo 必须作废。
    manager.executeCommand(std::make_unique<SetValueCommand>(value, 80));
    EXPECT_EQ(value, 80);
    EXPECT_TRUE(manager.canUndo());
    EXPECT_FALSE(manager.canRedo());

    // Redo 栈已清空，因此调用 redo() 不应把数值错误地改回 100。
    manager.redo();
    EXPECT_EQ(value, 80);
}

TEST(CommandManagerTest, EmptyHistoryDoesNothing)
{
    forge::application::CommandManager manager;

    // 空历史上的 Undo/Redo 是合法的无操作，不应抛出异常。
    EXPECT_NO_THROW(manager.undo());
    EXPECT_NO_THROW(manager.redo());
}

TEST(CommandManagerTest, NullCommandIsRejected)
{
    forge::application::CommandManager manager;

    // nullptr 没有可执行对象，必须在进入历史记录前被拒绝。
    EXPECT_THROW(manager.executeCommand(nullptr), std::invalid_argument);
    EXPECT_FALSE(manager.canUndo());
    EXPECT_FALSE(manager.canRedo());
}

} // namespace
