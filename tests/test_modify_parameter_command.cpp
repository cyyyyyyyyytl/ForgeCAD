#include "application/CommandManager.h"
#include "application/ModifyParameterCommand.h"
#include "domain/BoxFeature.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace {

// 从 Feature 的参数列表按名字读取数值。
// 测试不依赖参数排列位置，避免未来调整参数顺序后产生无关失败。
double parameterValue(const forge::domain::Feature& feature,
                      const std::string& parameterName)
{
    for (const auto& parameter : feature.parameters()) {
        if (parameter.name() == parameterName) {
            return parameter.asDouble();
        }
    }
    throw std::invalid_argument("测试找不到参数: " + parameterName);
}

TEST(ModifyParameterCommandTest, ExecuteUndoRedoChangesRealFeatureParameter)
{
    // 创建一个真实 Box；length 初始值是 50。
    forge::domain::BoxFeature box("Box001", 50.0, 40.0, 30.0);
    forge::application::CommandManager manager;

    // 创建并执行命令：length 从 50 改成 100，命令进入 Undo 栈。
    manager.executeCommand(
        std::make_unique<forge::application::ModifyParameterCommand>(
            box, "length", 100.0));
    EXPECT_DOUBLE_EQ(parameterValue(box, "length"), 100.0);
    EXPECT_TRUE(manager.canUndo());
    EXPECT_FALSE(manager.canRedo());

    // Undo 使用命令保存的 oldValue_，把 length 恢复成 50。
    manager.undo();
    EXPECT_DOUBLE_EQ(parameterValue(box, "length"), 50.0);
    EXPECT_FALSE(manager.canUndo());
    EXPECT_TRUE(manager.canRedo());

    // Redo 再次调用命令的 execute()，把 length 重新改成 100。
    manager.redo();
    EXPECT_DOUBLE_EQ(parameterValue(box, "length"), 100.0);
    EXPECT_TRUE(manager.canUndo());
    EXPECT_FALSE(manager.canRedo());
}

TEST(ModifyParameterCommandTest, UnknownParameterIsRejectedBeforeExecution)
{
    forge::domain::BoxFeature box("Box001", 50.0, 40.0, 30.0);

    // 构造时找不到参数便立即抛错，不产生一张无法撤销的坏命令卡。
    EXPECT_THROW(
        forge::application::ModifyParameterCommand(box, "radius", 100.0),
        std::invalid_argument);

    // 构造失败不应改变真实 Feature。
    EXPECT_DOUBLE_EQ(parameterValue(box, "length"), 50.0);
}

TEST(ModifyParameterCommandTest, OutOfRangeValueDoesNotChangeFeatureOrHistory)
{
    forge::domain::BoxFeature box("Box001", 50.0, 40.0, 30.0);
    forge::application::CommandManager manager;

    // Box.length 的 Catalog 最小值是 1；-5 在命令构造阶段就应被拒绝。
    EXPECT_THROW(
        manager.executeCommand(
            std::make_unique<forge::application::ModifyParameterCommand>(
                box, "length", -5.0)),
        std::invalid_argument);

    // 构造失败发生在 execute() 之前，因此模型仍是 50，Undo 历史也仍为空。
    EXPECT_DOUBLE_EQ(parameterValue(box, "length"), 50.0);
    EXPECT_FALSE(manager.canUndo());
    EXPECT_FALSE(manager.canRedo());
}

} // namespace
