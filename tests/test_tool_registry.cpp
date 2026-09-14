#include <gtest/gtest.h>

#include "application/ModelDocument.h"
#include "assistant/ToolRegistry.h"
#include "domain/Feature.h"

using forge::application::ModelDocument;
using forge::assistant::ToolRegistry;

// ============================================================
// ToolRegistryTest：验证 LLM JSON 到 ModelDocument 的安全边界
// ------------------------------------------------------------
// 测试完全不访问 DeepSeek 网络；直接构造模型可能返回的 QJsonObject，
// 因此结果稳定、无费用，也能精确定位是协议层还是网络层出错。
// ============================================================

// Registry 必须只暴露当前约定的四个白名单工具。
TEST(ToolRegistryTest, ExposesFourModelingTools)
{
    ModelDocument document;
    ToolRegistry registry(document);

    // 同时抽查第一个工具名，避免数组虽然为 4 但内容登记错误。
    const QJsonArray schemas = registry.schemas();
    ASSERT_EQ(schemas.size(), 4);
    EXPECT_EQ(schemas[0].toObject()["function"].toObject()["name"].toString(),
              "list_features");
}

// 模拟模型请求 create_feature，再用 list_features 查询刚创建的对象。
TEST(ToolRegistryTest, CreatesAndListsFeatureFromJsonArguments)
{
    ModelDocument document;
    ToolRegistry registry(document);

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
    EXPECT_TRUE(created["success"].toBool());
    EXPECT_TRUE(created["model_changed"].toBool());
    EXPECT_EQ(created["feature_id"].toString(), "Box001");
    ASSERT_NE(document.findFeature("Box001"), nullptr);

    // 查询工具不修改 Document，但应返回包含 Box001 的数组。
    const QJsonObject listed = registry.execute("list_features", {});
    EXPECT_TRUE(listed["success"].toBool());
    ASSERT_EQ(listed["features"].toArray().size(), 1);
}

// 验证成功修改、越界拒绝和未知工具拒绝三条分支。
TEST(ToolRegistryTest, ModifiesFeatureAndRejectsInvalidCalls)
{
    ModelDocument document;
    ToolRegistry registry(document);
    document.createFeature("Sphere", {{"radius", 20.0}});

    // 合法请求把 Sphere001.radius 从 20 修改为 35。
    const QJsonObject modified = registry.execute("set_parameter", {
        {"feature_id", "Sphere001"},
        {"parameter_name", "radius"},
        {"value", 35.0},
    });
    EXPECT_TRUE(modified["success"].toBool());
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
