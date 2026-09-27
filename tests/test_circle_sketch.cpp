#include <gtest/gtest.h>
#include "domain/CircleSketchFeature.h"
#include "geometry/ShapeFactory.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <limits>
#include <numbers>
#include "application/ModelDocument.h"
#include "infrastructure/NativeDocumentIO.h"
#include <QTemporaryDir>

TEST(CircleSketchTest, BuildsExactClosedCircleAtRequestedCenterAndExtrudes)
{
    forge::domain::CircleSketchFeature sketch("CircleSketch001", 10, -3, 4);
    const auto wire = sketch.rebuild();
    ASSERT_FALSE(wire.IsNull());
    ASSERT_EQ(wire.ShapeType(), TopAbs_WIRE);
    EXPECT_TRUE(TopoDS::Wire(wire).Closed());
    EXPECT_TRUE(BRepCheck_Analyzer(wire).IsValid());
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(wire, TopAbs_EDGE, edges);
    ASSERT_EQ(edges.Extent(), 1);
    BRepAdaptor_Curve curve(TopoDS::Edge(edges(1)));
    ASSERT_EQ(curve.GetType(), GeomAbs_Circle);
    EXPECT_DOUBLE_EQ(curve.Circle().Radius(), 10);
    EXPECT_DOUBLE_EQ(curve.Circle().Location().X(), -3);
    EXPECT_DOUBLE_EQ(curve.Circle().Location().Y(), 4);
    EXPECT_DOUBLE_EQ(curve.Circle().Location().Z(), 0);

    const auto solid = forge::geometry::ShapeFactory::extrudeWire(wire, 5);
    ASSERT_FALSE(solid.IsNull());
    EXPECT_EQ(solid.ShapeType(), TopAbs_SOLID);
    EXPECT_TRUE(BRepCheck_Analyzer(solid).IsValid());
    GProp_GProps properties;
    BRepGProp::VolumeProperties(solid, properties);
    EXPECT_NEAR(properties.Mass(), std::numbers::pi * 100 * 5, 1e-7);
    EXPECT_NEAR(properties.CentreOfMass().X(), -3, 1e-8);
    EXPECT_NEAR(properties.CentreOfMass().Y(), 4, 1e-8);
    EXPECT_NEAR(properties.CentreOfMass().Z(), 2.5, 1e-8);
}

TEST(CircleSketchTest, EditsRadiusAndRejectsInvalidParameters)
{
    forge::domain::CircleSketchFeature sketch("CircleSketch001", 10);
    sketch.setParameter("radius", 20.0);
    const auto wire = sketch.rebuild();
    ASSERT_FALSE(wire.IsNull());
    GProp_GProps properties;
    BRepGProp::LinearProperties(wire, properties);
    EXPECT_NEAR(properties.Mass(), 40 * std::numbers::pi, 1e-8);
    EXPECT_THROW(sketch.setParameter("width", 1.0), std::invalid_argument);
    sketch.setParameter("radius", 0.0);
    EXPECT_FALSE(sketch.validate().empty());
    EXPECT_TRUE(sketch.rebuild().IsNull());
    sketch.setParameter("radius", 10.0);
    sketch.setParameter("x", std::numeric_limits<double>::infinity());
    EXPECT_FALSE(sketch.validate().empty());
    EXPECT_TRUE(sketch.rebuild().IsNull());
    for (const auto radius : {0.0, -1.0, 1e-12,
         std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        EXPECT_TRUE(forge::geometry::ShapeFactory::makeCircleWire(radius).IsNull());
    }
}

TEST(CircleSketchTest, DocumentExtrusionHistoryAndNativeRoundTripPreserveDependency)
{
    forge::application::ModelDocument document;
    document.createFeature("CircleSketch", {{"radius", 10}, {"x", -3}, {"y", 4}});
    document.createExtrudeFeature("CircleSketch001", 5);
    auto volume = [](const forge::application::ModelDocument& model) {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(model.rebuildReport().at("Extrude001").shape, properties);
        return properties.Mass();
    };
    EXPECT_NEAR(volume(document), 500 * std::numbers::pi, 1e-7);
    EXPECT_EQ(document.visibleFeatureIds(), (std::vector<std::string>{"Extrude001"}));
    document.setParameter("CircleSketch001", "radius", 20);
    EXPECT_NEAR(volume(document), 2000 * std::numbers::pi, 1e-7);
    document.undo(); EXPECT_NEAR(volume(document), 500 * std::numbers::pi, 1e-7);
    document.redo(); EXPECT_NEAR(volume(document), 2000 * std::numbers::pi, 1e-7);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto path = dir.filePath("circle.forgecad");
    forge::infrastructure::NativeDocumentIO::save(document.exportData(), path);
    auto restored = forge::infrastructure::NativeDocumentIO::prepare(path);
    EXPECT_NEAR(volume(*restored), 2000 * std::numbers::pi, 1e-7);
    restored->setParameter("CircleSketch001", "radius", 10);
    EXPECT_NEAR(volume(*restored), 500 * std::numbers::pi, 1e-7);
    document.deleteFeature("CircleSketch001");
    EXPECT_TRUE(document.features().empty());
    document.undo(); EXPECT_NEAR(volume(document), 2000 * std::numbers::pi, 1e-7);
}
