#include <gtest/gtest.h>

#include "domain/FeatureCatalog.h"
#include "domain/FeatureFactory.h"

using namespace forge::domain;

TEST(FeatureCatalogTest, ContainsCurrentFeatureSchemas)
{
    const auto* box = FeatureCatalog::find("Box");
    ASSERT_NE(box, nullptr);
    ASSERT_EQ(box->parameters.size(), 3u);
    EXPECT_EQ(box->parameters[0].name, "length");
    EXPECT_DOUBLE_EQ(box->parameters[0].defaultValue, 100.0);

    EXPECT_NE(FeatureCatalog::find("Cylinder"), nullptr);
    EXPECT_NE(FeatureCatalog::find("Sphere"), nullptr);
    EXPECT_EQ(FeatureCatalog::find("Torus"), nullptr);
}

TEST(FeatureCatalogTest, NamedFactoryParametersDoNotDependOnMapOrder)
{
    NumericParameters parameters = {
        {"height", 30.0},
        {"length", 100.0},
        {"width", 50.0},
    };

    auto feature = FeatureFactory::createNamed("Box", "Box001", parameters);
    ASSERT_NE(feature, nullptr);
    EXPECT_DOUBLE_EQ(feature->parameters()[0].asDouble(), 100.0);
    EXPECT_DOUBLE_EQ(feature->parameters()[1].asDouble(), 50.0);
    EXPECT_DOUBLE_EQ(feature->parameters()[2].asDouble(), 30.0);
}

TEST(FeatureCatalogTest, NamedFactoryRejectsMissingUnknownAndOutOfRangeParameters)
{
    EXPECT_THROW(
        FeatureFactory::createNamed("Box", "Box001", NumericParameters{
            {"length", 100.0}, {"width", 50.0}}),
        std::invalid_argument);

    EXPECT_THROW(
        FeatureFactory::createNamed("Sphere", "Sphere001", NumericParameters{
            {"diameter", 20.0}}),
        std::invalid_argument);

    EXPECT_THROW(
        FeatureFactory::createNamed("Sphere", "Sphere001", NumericParameters{
            {"radius", -1.0}}),
        std::invalid_argument);
}
