#include <gtest/gtest.h>
#include "domain/RectangleSketchFeature.h"
#include "geometry/ShapeFactory.h"
#include "application/ModelDocument.h"
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <limits>

TEST(RectangleSketchTest, BuildsClosedFourEdgeWireInXYPlaneAtRequestedPosition)
{
    forge::domain::RectangleSketchFeature sketch("RectangleSketch001",10,20,-3,4);
    const auto shape=sketch.rebuild();
    ASSERT_FALSE(shape.IsNull());
    ASSERT_EQ(shape.ShapeType(),TopAbs_WIRE);
    EXPECT_TRUE(TopoDS::Wire(shape).Closed());
    EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());
    TopTools_IndexedMapOfShape edges,vertices,faces;
    TopExp::MapShapes(shape,TopAbs_EDGE,edges);
    TopExp::MapShapes(shape,TopAbs_VERTEX,vertices);
    TopExp::MapShapes(shape,TopAbs_FACE,faces);
    EXPECT_EQ(edges.Extent(),4); EXPECT_EQ(vertices.Extent(),4); EXPECT_EQ(faces.Extent(),0);
    GProp_GProps properties;
    BRepGProp::LinearProperties(shape,properties);
    EXPECT_NEAR(properties.Mass(),60,1e-8);
    EXPECT_NEAR(properties.CentreOfMass().X(),2,1e-8);
    EXPECT_NEAR(properties.CentreOfMass().Y(),14,1e-8);
    EXPECT_NEAR(properties.CentreOfMass().Z(),0,1e-8);
}

TEST(RectangleSketchTest, EditsParametersAndRejectsInvalidGeometry)
{
    forge::domain::RectangleSketchFeature sketch("RectangleSketch001",10,20);
    sketch.setParameter("width",30.0);
    EXPECT_DOUBLE_EQ(sketch.parameters()[1].asDouble(),30);
    EXPECT_THROW(sketch.setParameter("height",1.0),std::invalid_argument);
    sketch.setParameter("length",0.0);
    EXPECT_FALSE(sketch.validate().empty()); EXPECT_TRUE(sketch.rebuild().IsNull());
    sketch.setParameter("length",10.0);
    sketch.setParameter("x",std::numeric_limits<double>::infinity());
    EXPECT_FALSE(sketch.validate().empty()); EXPECT_TRUE(sketch.rebuild().IsNull());
    EXPECT_TRUE(forge::geometry::ShapeFactory::makeRectangleWire(1e-12,10).IsNull());
}

TEST(RectangleSketchTest, DocumentCreationAndHistoryRebuildTheEditedWire)
{
    forge::application::ModelDocument document;
    auto& feature=document.createFeature("RectangleSketch",{{"length",10},{"width",20},{"x",-3}});
    EXPECT_EQ(feature.id(),"RectangleSketch001");
    EXPECT_DOUBLE_EQ(feature.parameters()[3].asDouble(),0);
    auto perimeter=[&]() {
        const auto report=document.rebuildReport();
        const auto& result=report.at("RectangleSketch001");
        EXPECT_EQ(result.status,forge::core::RebuildStatus::Ready);
        GProp_GProps properties;
        BRepGProp::LinearProperties(result.shape,properties);
        return properties.Mass();
    };
    EXPECT_NEAR(perimeter(),60,1e-8);
    document.setParameter("RectangleSketch001","width",30);
    EXPECT_NEAR(perimeter(),80,1e-8);
    document.undo(); EXPECT_NEAR(perimeter(),60,1e-8);
    document.redo(); EXPECT_NEAR(perimeter(),80,1e-8);
    document.deleteFeature("RectangleSketch001");
    EXPECT_TRUE(document.features().empty());
    document.undo(); EXPECT_NEAR(perimeter(),80,1e-8);
}
