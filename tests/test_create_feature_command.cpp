#include "application/CommandManager.h"
#include "application/CreateFeatureCommand.h"
#include "application/ModelDocument.h"
#include "domain/FeatureFactory.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace {

TEST(CreateFeatureCommandTest, ExecuteUndoRedoTransfersFeatureOwnership)
{
    forge::application::ModelDocument document;
    forge::application::CommandManager manager;
    auto feature = forge::domain::FeatureFactory::create(
        "Box", "Box001", {50.0, 40.0, 30.0});
    const auto* originalAddress = feature.get();

    // Execute：所有权从命令转入 Document，Feature 可以按稳定 ID 找到。
    manager.executeCommand(
        std::make_unique<forge::application::CreateFeatureCommand>(
            document, std::move(feature)));
    EXPECT_EQ(document.findFeature("Box001"), originalAddress);
    EXPECT_TRUE(manager.canUndo());

    // Undo：所有权由 Document 取回命令，文档中不再显示这个 Feature。
    manager.undo();
    EXPECT_EQ(document.findFeature("Box001"), nullptr);
    EXPECT_TRUE(manager.canRedo());

    // Redo：同一个堆对象再次回到 Document，没有重新构造，也没有更换地址。
    manager.redo();
    EXPECT_EQ(document.findFeature("Box001"), originalAddress);
    EXPECT_TRUE(manager.canUndo());
    EXPECT_FALSE(manager.canRedo());
}

TEST(CreateFeatureCommandTest, NullFeatureIsRejected)
{
    forge::application::ModelDocument document;

    // 空 Feature 无法执行或撤销，必须在命令构造阶段拒绝。
    EXPECT_THROW(
        forge::application::CreateFeatureCommand(document, nullptr),
        std::invalid_argument);
    EXPECT_TRUE(document.features().empty());
}

} // namespace
