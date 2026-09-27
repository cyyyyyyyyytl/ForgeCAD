#include <gtest/gtest.h>
#include "application/ModelDocument.h"
#include "domain/ExtrudeFeature.h"
#include "domain/Feature.h"
#include "geometry/ShapeFactory.h"
#include "infrastructure/NativeDocumentIO.h"
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <QTemporaryDir>
#include <limits>
using forge::application::ModelDocument;
using forge::geometry::ShapeFactory;
namespace {
double volume(const TopoDS_Shape& shape) {
    GProp_GProps props; BRepGProp::VolumeProperties(shape,props); return props.Mass();
}
}
TEST(ExtrudeFeatureTest, ExtrudesPositionedSketchToValidSolidAndRejectsWrongInputs) {
    const auto wire = ShapeFactory::translate(ShapeFactory::makeRectangleWire(10,20),-3,4,0);
    forge::domain::ExtrudeFeature feature("Extrude001",5);
    const auto shape = feature.rebuild({wire});
    ASSERT_FALSE(shape.IsNull()); EXPECT_EQ(shape.ShapeType(),TopAbs_SOLID);
    EXPECT_EQ(ShapeFactory::inspectShape(shape).status,forge::core::RebuildStatus::Ready);
    GProp_GProps props; BRepGProp::VolumeProperties(shape,props);
    EXPECT_NEAR(props.Mass(),1000,1e-8);
    EXPECT_NEAR(props.CentreOfMass().X(),2,1e-8);
    EXPECT_NEAR(props.CentreOfMass().Y(),14,1e-8);
    EXPECT_NEAR(props.CentreOfMass().Z(),2.5,1e-8);
    EXPECT_TRUE(feature.rebuild().IsNull()); EXPECT_TRUE(feature.rebuild({wire,wire}).IsNull());
    EXPECT_TRUE(feature.rebuild({ShapeFactory::makeBox(2,2,2)}).IsNull());
    BRepBuilderAPI_MakePolygon open; open.Add(gp_Pnt(0,0,0)); open.Add(gp_Pnt(10,0,0));
    EXPECT_TRUE(feature.rebuild({open.Wire()}).IsNull());
    feature.setParameter("height",0.0); EXPECT_FALSE(feature.validate().empty());
    EXPECT_TRUE(feature.rebuild({wire}).IsNull());
    EXPECT_THROW(feature.setParameter("width",5.0),std::invalid_argument);
}
TEST(ExtrudeDocumentTest, UpstreamEditsVisibilityAndCascadeDeletionFollowHistory) {
    ModelDocument document;
    document.createFeature("RectangleSketch",{{"length",10},{"width",20}});
    auto& feature = document.createExtrudeFeature("RectangleSketch001",5);
    EXPECT_EQ(feature.id(),"Extrude001");
    EXPECT_EQ(document.visibleFeatureIds(),(std::vector<std::string>{"Extrude001"}));
    auto mass = [&]() { return volume(document.rebuildReport().at("Extrude001").shape); };
    EXPECT_NEAR(mass(),1000,1e-8);
    document.undo(); EXPECT_EQ(document.visibleFeatureIds(),(std::vector<std::string>{"RectangleSketch001"}));
    document.redo(); EXPECT_NEAR(mass(),1000,1e-8);
    document.setParameter("RectangleSketch001","length",30); EXPECT_NEAR(mass(),3000,1e-8);
    document.setParameter("Extrude001","height",8); EXPECT_NEAR(mass(),4800,1e-8);
    document.undo(); EXPECT_NEAR(mass(),3000,1e-8);
    document.redo(); EXPECT_NEAR(mass(),4800,1e-8);
    document.deleteFeature("RectangleSketch001"); EXPECT_TRUE(document.features().empty());
    document.undo(); EXPECT_NEAR(mass(),4800,1e-8);
}
TEST(ExtrudeDocumentTest, InvalidCreationPreservesRedoAndFixedInputRoles) {
    ModelDocument document;
    document.createFeature("RectangleSketch",{{"length",10},{"width",20}});
    document.createFeature("Box",{{"length",1},{"width",1},{"height",1}});
    document.undo(); ASSERT_TRUE(document.canRedo());
    EXPECT_THROW(document.createExtrudeFeature("missing",5),std::invalid_argument);
    EXPECT_THROW(document.createExtrudeFeature("RectangleSketch001",0),std::invalid_argument);
    EXPECT_THROW(document.createExtrudeFeature("RectangleSketch001",std::numeric_limits<double>::infinity()),std::invalid_argument);
    EXPECT_TRUE(document.canRedo());
    document.redo();
    EXPECT_THROW(document.createExtrudeFeature("Box001",5),std::invalid_argument);
    document.createExtrudeFeature("RectangleSketch001",5);
    EXPECT_THROW(document.addDependency("Extrude001","Box001"),std::invalid_argument);
}
TEST(ExtrudeDocumentTest, NativeRoundTripPreservesSketchDependencyAndEditableDimensions) {
    ModelDocument original;
    original.createFeature("RectangleSketch",{{"length",10},{"width",20},{"x",-3}});
    original.createExtrudeFeature("RectangleSketch001",5);
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const auto path = dir.filePath("part.forgecad");
    forge::infrastructure::NativeDocumentIO::save(original.exportData(),path);
    auto restored = forge::infrastructure::NativeDocumentIO::prepare(path);
    EXPECT_EQ(restored->visibleFeatureIds(),(std::vector<std::string>{"Extrude001"}));
    EXPECT_NEAR(volume(restored->rebuildReport().at("Extrude001").shape),1000,1e-8);
    restored->setParameter("RectangleSketch001","width",30);
    EXPECT_NEAR(volume(restored->rebuildReport().at("Extrude001").shape),1500,1e-8);
    auto bad = original.exportData(); bad.features[1].dependencies.clear();
    EXPECT_THROW(restored->replaceData(bad),std::invalid_argument);
}

TEST(ExtrudeDocumentTest, DirectionsPreserveTotalHeightAndRejectInvalidValues) {
    ModelDocument document;
    document.createFeature("RectangleSketch",{{"length",10},{"width",20}});
    document.createExtrudeFeature("RectangleSketch001",20);
    auto check = [&](double z) {
        const auto shape = document.rebuildReport().at("Extrude001").shape;
        ASSERT_FALSE(shape.IsNull());
        GProp_GProps props; BRepGProp::VolumeProperties(shape,props);
        EXPECT_NEAR(props.Mass(),4000,1e-7);
        EXPECT_NEAR(props.CentreOfMass().Z(),z,1e-8);
        EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());
        Bnd_Box bounds; BRepBndLib::Add(shape,bounds);
        double xmin,ymin,zmin,xmax,ymax,zmax;
        bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
        EXPECT_NEAR(zmin,z-10,1e-5);
        EXPECT_NEAR(zmax,z+10,1e-5);
    };
    check(10);
    document.setParameter("Extrude001","direction",1); check(-10);
    document.setParameter("Extrude001","direction",2); check(0);
    document.undo(); check(-10);
    EXPECT_THROW(document.setParameter("Extrude001","direction",0.5),std::invalid_argument);
    EXPECT_TRUE(document.canRedo()); check(-10);
    document.redo(); check(0);
    EXPECT_THROW(document.createExtrudeFeature("RectangleSketch001",20,
        static_cast<forge::domain::ExtrudeDirection>(3)),std::invalid_argument);
    EXPECT_EQ(document.features().size(),2u);
    for (const auto direction : {1.0,2.0}) {
        document.setParameter("Extrude001","direction",direction);
        QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
        const auto path=dir.filePath("direction.forgecad");
        forge::infrastructure::NativeDocumentIO::save(document.exportData(),path);
        auto restored=forge::infrastructure::NativeDocumentIO::prepare(path);
        EXPECT_DOUBLE_EQ(restored->findFeature("Extrude001")->parameters()[1].asDouble(),direction);
        GProp_GProps props;
        BRepGProp::VolumeProperties(restored->rebuildReport().at("Extrude001").shape,props);
        EXPECT_NEAR(props.CentreOfMass().Z(),direction == 1 ? -10 : 0,1e-8);
    }
    auto legacy=document.exportData(); legacy.features[1].parameters.erase("direction");
    ModelDocument restored; restored.replaceData(legacy);
    EXPECT_DOUBLE_EQ(restored.findFeature("Extrude001")->parameters()[1].asDouble(),0);
    auto malformed=document.exportData(); malformed.features[1].parameters["direction"]=1.5;
    EXPECT_THROW(restored.replaceData(malformed),std::invalid_argument);
}
