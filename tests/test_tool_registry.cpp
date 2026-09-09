#include <gtest/gtest.h>

#include "application/ModelDocument.h"
#include "application/ModelingService.h"
#include "assistant/ToolRegistry.h"
#include "domain/Feature.h"

using forge::application::ModelDocument;
using forge::application::ModelingService;
using forge::assistant::ToolRegistry;

TEST(ToolRegistryTest, ExposesFourModelingTools)
{
    ModelDocument document;
    ModelingService service(document);
    ToolRegistry registry(document, service);

    const QJsonArray schemas = registry.schemas();
    ASSERT_EQ(schemas.size(), 4);
    EXPECT_EQ(schemas[0].toObject()["function"].toObject()["name"].toString(),
              "list_features");
}

TEST(ToolRegistryTest, CreatesAndListsFeatureFromJsonArguments)
{
    ModelDocument document;
    ModelingService service(document);
    ToolRegistry registry(document, service);

    const QJsonObject created = registry.execute("create_feature", {
        {"type", "Box"},
        {"parameters", QJsonObject{
            {"length", 100.0},
            {"width", 50.0},
            {"height", 30.0},
        }},
    });

    EXPECT_TRUE(created["success"].toBool());
    EXPECT_TRUE(created["model_changed"].toBool());
    EXPECT_EQ(created["feature_id"].toString(), "Box001");
    ASSERT_NE(document.findFeature("Box001"), nullptr);

    const QJsonObject listed = registry.execute("list_features", {});
    EXPECT_TRUE(listed["success"].toBool());
    ASSERT_EQ(listed["features"].toArray().size(), 1);
}

TEST(ToolRegistryTest, ModifiesFeatureAndRejectsInvalidCalls)
{
    ModelDocument document;
    ModelingService service(document);
    ToolRegistry registry(document, service);
    service.createFeature("Sphere", {{"radius", 20.0}});

    const QJsonObject modified = registry.execute("set_parameter", {
        {"feature_id", "Sphere001"},
        {"parameter_name", "radius"},
        {"value", 35.0},
    });
    EXPECT_TRUE(modified["success"].toBool());
    EXPECT_DOUBLE_EQ(document.findFeature("Sphere001")->parameters()[0].asDouble(), 35.0);

    EXPECT_FALSE(registry.execute("set_parameter", {
        {"feature_id", "Sphere001"},
        {"parameter_name", "radius"},
        {"value", -1.0},
    })["success"].toBool());
    EXPECT_FALSE(registry.execute("unknown_tool", {})["success"].toBool());
}
