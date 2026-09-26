#include <gtest/gtest.h>
#include "application/ModelDocument.h"
#include "assistant/ToolRegistry.h"
#include "domain/BoxFeature.h"
#include "domain/CylinderFeature.h"
#include "domain/SphereFeature.h"
#include "domain/BooleanFeature.h"
#include "domain/FeatureRegistry.h"
#include "geometry/ShapeFactory.h"
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <limits>

using forge::application::ModelDocument;
using forge::domain::BooleanOperation;
namespace {
GProp_GProps properties(const TopoDS_Shape& shape)
{
    GProp_GProps result;
    BRepGProp::VolumeProperties(shape, result);
    return result;
}
void expectSolid(const TopoDS_Shape& shape, double volume, double x, double y, double z)
{
    ASSERT_FALSE(shape.IsNull());
    EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());
    const auto p = properties(shape);
    EXPECT_NEAR(p.Mass(), volume, 1e-5);
    EXPECT_NEAR(p.CentreOfMass().X(), x, 1e-5);
    EXPECT_NEAR(p.CentreOfMass().Y(), y, 1e-5);
    EXPECT_NEAR(p.CentreOfMass().Z(), z, 1e-5);
}
}

// 检查实际几何的质心和体积，证明移动改变坐标且不改变尺寸。
TEST(PositionTest, PrimitiveReferencePointsAndNegativeCoordinates)
{
    forge::domain::BoxFeature box("B", 10, 20, 30, -15, 2.125, -7);
    expectSolid(box.rebuild(), 6000, -10, 12.125, 8);
    forge::domain::CylinderFeature cylinder("C", 2, 10, -15, 2.125, -7);
    expectSolid(cylinder.rebuild(), 40 * std::acos(-1.0), -15, 2.125, -2);
    forge::domain::SphereFeature sphere("S", 3, -15, 2.125, -7);
    expectSolid(sphere.rebuild(), 36 * std::acos(-1.0), -15, 2.125, -7);
}

TEST(PositionTest, OmittedCoordinatesDefaultToZeroAndPartialCoordinatesWork)
{
    auto sphere = forge::domain::FeatureRegistry::create("Sphere", "S", {{"radius", 3}, {"y", -2}});
    expectSolid(sphere->rebuild(), 36 * std::acos(-1.0), 0, -2, 0);
    auto box = forge::domain::FeatureRegistry::create("Box", "B", {{"length", 10}, {"width", 20}, {"height", 30}});
    expectSolid(box->rebuild(), 6000, 5, 10, 15);
    EXPECT_THROW(forge::domain::FeatureRegistry::create("Box", "B", {{"x", 1}}), std::invalid_argument);
    EXPECT_THROW(forge::domain::FeatureRegistry::create("Sphere", "S", {{"radius", 3}, {"xx", 1}}), std::invalid_argument);
}

TEST(PositionTest, OffsetHoleMovesAndHistoryRestoresBooleanGeometry)
{
    ModelDocument document;
    document.createFeature("Box", {{"length", 10}, {"width", 10}, {"height", 10}});
    document.createFeature("Cylinder", {{"radius", 1}, {"height", 12}, {"x", 3}, {"y", 4}, {"z", -1}});
    document.createBooleanFeature(BooleanOperation::Difference, "Box001", "Cylinder001");
    const double drilledVolume = 1000 - 10 * std::acos(-1.0);
    EXPECT_NEAR(properties(document.rebuildShapes().at("Cut001")).Mass(), drilledVolume, 1e-5);
    document.setParameter("Cylinder001", "x", 20);
    EXPECT_NEAR(properties(document.rebuildShapes().at("Cut001")).Mass(), 1000, 1e-5);
    document.undo();
    EXPECT_NEAR(properties(document.rebuildShapes().at("Cut001")).Mass(), drilledVolume, 1e-5);
    document.redo();
    EXPECT_NEAR(properties(document.rebuildShapes().at("Cut001")).Mass(), 1000, 1e-5);
    document.deleteFeature("Box001");
    document.undo();
    expectSolid(document.rebuildShapes().at("Cylinder001"), 12 * std::acos(-1.0), 20, 4, 5);
}

TEST(PositionTest, MovementRecomputesUnionAndIntersection)
{
    for (auto operation : {BooleanOperation::Union, BooleanOperation::Intersection}) {
        ModelDocument document;
        document.createFeature("Box", {{"length", 10}, {"width", 10}, {"height", 10}});
        document.createFeature("Box", {{"length", 10}, {"width", 10}, {"height", 10}, {"x", 5}});
        const std::string id = document.createBooleanFeature(operation, "Box001", "Box002").id();
        EXPECT_NEAR(properties(document.rebuildShapes().at(id)).Mass(), operation == BooleanOperation::Union ? 1500 : 500, 1e-5);
        document.setParameter("Box002", "x", 7);
        EXPECT_NEAR(properties(document.rebuildShapes().at(id)).Mass(), operation == BooleanOperation::Union ? 1700 : 300, 1e-5);
    }
}

TEST(PositionTest, RejectsNonFiniteAndOutOfRangeWithoutChangingHistory)
{
    ModelDocument document;
    document.createFeature("Sphere", {{"radius", 3}});
    document.setParameter("Sphere001", "x", -2);
    document.undo(); // 保留 Redo，以检查失败操作没有污染历史。
    for (double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -1000001.0, 1000001.0}) {
        EXPECT_THROW(document.setParameter("Sphere001", "x", value), std::invalid_argument);
        EXPECT_THROW(document.createFeature("Sphere", {{"radius", 3}, {"z", value}}), std::invalid_argument);
        EXPECT_TRUE(document.canRedo());
    }
    EXPECT_THROW(document.setParameter("Sphere001", "radius", std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    document.redo();
    expectSolid(document.rebuildShapes().at("Sphere001"), 36 * std::acos(-1.0), -2, 0, 0);
    forge::domain::BoxFeature invalid("B", 1, 1, 1);
    invalid.setParameter("z", std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(invalid.validate().empty());
    EXPECT_TRUE(invalid.rebuild().IsNull());
    EXPECT_TRUE(forge::geometry::ShapeFactory::translate({}, 1, 2, 3).IsNull());
}

TEST(PositionTest, AiToolsCreateQueryAndEditWorldPosition)
{
    ModelDocument document;
    forge::assistant::ToolRegistry tools(document);
    auto result = tools.execute("create_feature", {{"type", "Sphere"}, {"parameters", QJsonObject{{"radius", 3}, {"x", -2.125}}}});
    ASSERT_TRUE(result["success"].toBool());
    EXPECT_DOUBLE_EQ(result["parameters"].toObject()["x"].toDouble(), -2.125);
    result = tools.execute("set_parameter", {{"feature_id", "Sphere001"}, {"parameter_name", "z"}, {"value", -4.5}});
    ASSERT_TRUE(result["success"].toBool());
    expectSolid(document.rebuildShapes().at("Sphere001"), 36 * std::acos(-1.0), -2.125, 0, -4.5);
}
