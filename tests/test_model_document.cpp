#include <gtest/gtest.h>

#include "application/ModelDocument.h"
#include "domain/Feature.h"

using forge::application::ModelDocument;

namespace {

double parameterValue(const ModelDocument& document,
                      std::string_view featureId,
                      std::size_t index)
{
    const auto* feature = document.findFeature(featureId);
    EXPECT_NE(feature, nullptr);
    return feature ? feature->parameters().at(index).asDouble() : 0.0;
}

} // namespace

TEST(ModelDocumentTest, CreatesAndFindsFeaturesWithStableIds)
{
    ModelDocument document;
    auto& firstBox = document.createFeature("Box", {
        {"length", 100.0}, {"width", 50.0}, {"height", 30.0},
    });
    auto& sphere = document.createFeature("Sphere", {{"radius", 20.0}});
    auto& secondBox = document.createFeature("Box", {
        {"length", 20.0}, {"width", 20.0}, {"height", 20.0},
    });

    EXPECT_EQ(firstBox.id(), "Box001");
    EXPECT_EQ(sphere.id(), "Sphere001");
    EXPECT_EQ(secondBox.id(), "Box002");
    EXPECT_EQ(document.features().size(), 3u);
    EXPECT_EQ(document.findFeature("missing"), nullptr);
}

TEST(ModelDocumentTest, CreateAndParameterChangeUndoInLifoOrder)
{
    ModelDocument document;
    document.createFeature("Sphere", {{"radius", 20.0}});
    document.setParameter("Sphere001", "radius", 35.0);

    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 35.0);

    document.undo();
    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 20.0);

    document.undo();
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr);

    document.redo();
    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 20.0);
    document.redo();
    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 35.0);
}

TEST(ModelDocumentTest, DeleteUndoRestoresFeatureAndOrder)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 20.0}, {"height", 30.0},
    });
    document.createFeature("Sphere", {{"radius", 20.0}});
    document.createFeature("Cylinder", {{"radius", 15.0}, {"height", 40.0}});

    document.deleteFeature("Sphere001");
    ASSERT_EQ(document.features().size(), 2u);
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr);

    document.undo();
    ASSERT_EQ(document.features().size(), 3u);
    EXPECT_EQ(document.features()[0]->id(), "Box001");
    EXPECT_EQ(document.features()[1]->id(), "Sphere001");
    EXPECT_EQ(document.features()[2]->id(), "Cylinder001");

    document.redo();
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr);
}

TEST(ModelDocumentTest, InvalidOperationDoesNotPolluteHistory)
{
    ModelDocument document;
    document.createFeature("Sphere", {{"radius", 20.0}});

    EXPECT_THROW(
        document.setParameter("Sphere001", "radius", -5.0),
        std::invalid_argument);
    EXPECT_THROW(
        document.setParameter("Sphere001", "height", 10.0),
        std::invalid_argument);
    EXPECT_THROW(document.deleteFeature("missing"), std::invalid_argument);
    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 20.0);

    document.undo();
    EXPECT_TRUE(document.features().empty());
    EXPECT_FALSE(document.canUndo());
}

TEST(ModelDocumentTest, NewChangeClearsRedoHistory)
{
    ModelDocument document;
    document.createFeature("Sphere", {{"radius", 20.0}});
    document.setParameter("Sphere001", "radius", 30.0);
    document.undo();
    ASSERT_TRUE(document.canRedo());

    document.setParameter("Sphere001", "radius", 40.0);
    EXPECT_FALSE(document.canRedo());
    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 40.0);
}
