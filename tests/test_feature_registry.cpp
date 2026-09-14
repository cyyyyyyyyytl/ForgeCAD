#include <gtest/gtest.h>

#include "domain/Feature.h"
#include "domain/FeatureRegistry.h"

using forge::domain::FeatureRegistry;
using forge::domain::NumericParameters;

TEST(FeatureRegistryTest, DescribesAllSupportedFeatures)
{
    EXPECT_EQ(FeatureRegistry::all().size(), 3u);

    const auto* box = FeatureRegistry::find("Box");
    ASSERT_NE(box, nullptr);
    ASSERT_EQ(box->parameters.size(), 3u);
    EXPECT_EQ(box->parameters[0].name, "length");
    EXPECT_NE(FeatureRegistry::find("Cylinder"), nullptr);
    EXPECT_NE(FeatureRegistry::find("Sphere"), nullptr);
    EXPECT_EQ(FeatureRegistry::find("Torus"), nullptr);
}

TEST(FeatureRegistryTest, CreatesFeatureFromNamedParameters)
{
    auto feature = FeatureRegistry::create("Box", "Box001", {
        {"height", 30.0},
        {"length", 100.0},
        {"width", 50.0},
    });

    ASSERT_NE(feature, nullptr);
    EXPECT_EQ(feature->id(), "Box001");
    EXPECT_EQ(feature->type(), "Box");
    EXPECT_FALSE(feature->rebuild().IsNull());
}

TEST(FeatureRegistryTest, RejectsUnknownMissingAndOutOfRangeValues)
{
    EXPECT_THROW(
        FeatureRegistry::create("Torus", "Torus001", {{"radius", 10.0}}),
        std::invalid_argument);
    EXPECT_THROW(
        FeatureRegistry::create("Sphere", "Sphere001", {{"wrong", 20.0}}),
        std::invalid_argument);
    EXPECT_THROW(
        FeatureRegistry::create("Sphere", "Sphere001", {{"radius", -1.0}}),
        std::invalid_argument);
}
