#include <gtest/gtest.h> // GoogleTest 的测试定义和断言宏。

#include "application/ModelDocument.h" // 工具最终读写的真实文档。
#include "assistant/ToolRegistry.h"     // 被测的 JSON 安全边界。
#include "domain/Feature.h"             // 读取工具修改后的领域参数。

using forge::application::ModelDocument;
using forge::assistant::ToolRegistry;

// ============================================================
// ToolRegistryTest：验证 LLM JSON 到 ModelDocument 的安全边界
// ------------------------------------------------------------
// 测试完全不访问 DeepSeek 网络；直接构造模型可能返回的 QJsonObject，
// 因此结果稳定、无费用，也能精确定位是协议层还是网络层出错。
// ============================================================

// Registry 必须只暴露当前约定的五个白名单工具。
TEST(ToolRegistryTest, ExposesFiveModelingTools)
{
    ModelDocument document;         // 使用空文档隔离本测试。
    ToolRegistry registry(document); // Registry 保存对该文档的非拥有引用。

    // 同时抽查首尾工具名，避免数量正确但删除工具未正确登记。
    const QJsonArray schemas = registry.schemas();
    ASSERT_EQ(schemas.size(), 5); // 工具数量不符时停止，避免下面访问数组越界。
    EXPECT_EQ(schemas[0].toObject()["function"].toObject()["name"].toString(),
              "list_features");
    EXPECT_EQ(schemas[4].toObject()["function"].toObject()["name"].toString(),
              "delete_feature");
}

// 模拟模型请求 create_feature，再用 list_features 查询刚创建的对象。
TEST(ToolRegistryTest, CreatesAndListsFeatureFromJsonArguments)
{
    ModelDocument document;          // 创建工具所操作的实际文档。
    ToolRegistry registry(document); // 把文档注入 AI 工具边界。

    // JSON 结构与 DeepSeek function.arguments 解析后的对象一致。
    const QJsonObject created = registry.execute("create_feature", {
        {"type", "Box"},
        {"parameters", QJsonObject{
            {"length", 100.0},
            {"width", 50.0},
            {"height", 30.0},
        }},
    });

    // 除 success 外，修改工具必须标记 model_changed，UI 才会刷新三维视图。
    EXPECT_TRUE(created["success"].toBool());              // 本地创建执行成功。
    EXPECT_TRUE(created["model_changed"].toBool());        // Controller 应刷新 UI。
    EXPECT_EQ(created["feature_id"].toString(), "Box001"); // 返回稳定 ID 给模型。
    ASSERT_NE(document.findFeature("Box001"), nullptr);     // 文档中确实存在对象。

    // 查询工具不修改 Document，但应返回包含 Box001 的数组。
    const QJsonObject listed = registry.execute("list_features", {});
    EXPECT_TRUE(listed["success"].toBool());          // 查询本身成功。
    ASSERT_EQ(listed["features"].toArray().size(), 1); // 返回刚创建的唯一对象。
}

// 验证成功修改、越界拒绝和未知工具拒绝三条分支。
TEST(ToolRegistryTest, ModifiesFeatureAndRejectsInvalidCalls)
{
    ModelDocument document;          // 为修改工具准备独立文档。
    ToolRegistry registry(document); // 工具持有文档引用。
    document.createFeature("Sphere", {{"radius", 20.0}}); // 建立待修改对象。

    // 合法请求把 Sphere001.radius 从 20 修改为 35。
    const QJsonObject modified = registry.execute("set_parameter", {
        {"feature_id", "Sphere001"},
        {"parameter_name", "radius"},
        {"value", 35.0},
    });
    EXPECT_TRUE(modified["success"].toBool()); // Registry 已执行 Document 修改。
    EXPECT_DOUBLE_EQ(document.findFeature("Sphere001")->parameters()[0].asDouble(), 35.0);

    // 非法负半径应返回 success=false，而不是让异常逃出 Registry。
    EXPECT_FALSE(registry.execute("set_parameter", {
        {"feature_id", "Sphere001"},
        {"parameter_name", "radius"},
        {"value", -1.0},
    })["success"].toBool());
    // 工具白名单外的名称必须拒绝，防止模型调用未授权能力。
    EXPECT_FALSE(registry.execute("unknown_tool", {})["success"].toBool());
}

// 删除工具要报告已删除的 ID，并允许用户随后通过 Document::undo() 恢复。
TEST(ToolRegistryTest, DeletesFeatureAndCanUndo)
{
    ModelDocument document;
    ToolRegistry registry(document);
    document.createFeature("Sphere", {{"radius", 20.0}});

    const QJsonObject deleted = registry.execute("delete_feature", {{"feature_id", "Sphere001"}});
    EXPECT_TRUE(deleted["success"].toBool());
    EXPECT_TRUE(deleted["model_changed"].toBool());
    EXPECT_EQ(deleted["feature_id"].toString(), "Sphere001");
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr);

    document.undo();
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);
}

// 非法目标不应删除已有特征，也不应新增一次撤销记录。
TEST(ToolRegistryTest, RejectsInvalidDeleteWithoutChangingDocument)
{
    ModelDocument document;
    ToolRegistry registry(document);
    document.createFeature("Sphere", {{"radius", 20.0}});

    EXPECT_FALSE(registry.execute("delete_feature", {})["success"].toBool());
    EXPECT_FALSE(registry.execute("delete_feature", {{"feature_id", "Sphere999"}})["success"].toBool());
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);

    // 撤销栈应仍只含最初的创建：一次 undo 后文档应为空。
    document.undo();
    EXPECT_TRUE(document.features().empty());
    EXPECT_FALSE(document.canUndo());
}
