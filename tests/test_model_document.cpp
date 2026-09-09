#include <gtest/gtest.h>

#include "application/ModelDocument.h"
#include "domain/FeatureFactory.h"

using forge::application::ModelDocument;
using forge::domain::FeatureFactory;

TEST(ModelDocumentTest, GeneratesIndependentStableIdsByType)
{
    ModelDocument document;
    EXPECT_EQ(document.nextFeatureId("Box"), "Box001");
    EXPECT_EQ(document.nextFeatureId("Box"), "Box002");
    EXPECT_EQ(document.nextFeatureId("Sphere"), "Sphere001");
}

TEST(ModelDocumentTest, GeneratedIdSkipsAnExistingImportedId)
{
    ModelDocument document;
    document.addFeature(FeatureFactory::create("Box", "Box001", {10.0, 20.0, 30.0}));

    EXPECT_EQ(document.nextFeatureId("Box"), "Box002");
}

TEST(ModelDocumentTest, OwnsAndFindsFeaturesById)
{
    ModelDocument document;
    auto& added = document.addFeature(
        FeatureFactory::create("Sphere", "Sphere001", {20.0}));

    EXPECT_EQ(added.id(), "Sphere001");
    EXPECT_EQ(document.features().size(), 1u);
    EXPECT_EQ(document.findFeature("Sphere001"), &added);
    EXPECT_EQ(document.findFeature("missing"), nullptr);
}

TEST(ModelDocumentTest, RejectsNullAndDuplicateFeatures)
{
    ModelDocument document;
    EXPECT_THROW(document.addFeature(nullptr), std::invalid_argument);

    document.addFeature(FeatureFactory::create("Sphere", "Sphere001", {20.0}));
    EXPECT_THROW(
        document.addFeature(FeatureFactory::create("Sphere", "Sphere001", {30.0})),
        std::invalid_argument);
}
