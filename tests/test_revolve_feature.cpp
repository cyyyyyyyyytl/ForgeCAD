#include <gtest/gtest.h>
#include "application/ModelDocument.h"
#include "domain/RevolveFeature.h"
#include "geometry/ShapeFactory.h"
#include "infrastructure/NativeDocumentIO.h"
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <QTemporaryDir>
#include <numbers>
#include <limits>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
using forge::application::ModelDocument;
using forge::domain::RevolveAxis;
using forge::geometry::ShapeFactory;
namespace {
double volume(const TopoDS_Shape& shape) {
    EXPECT_FALSE(shape.IsNull()); if (shape.IsNull()) return 0;
    EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());
    GProp_GProps props; BRepGProp::VolumeProperties(shape,props); return props.Mass();
}
}
TEST(RevolveTest, CircleAndRectangleGenerateFullAndPartialSolids)
{
    const auto circle=ShapeFactory::translate(ShapeFactory::makeCircleWire(2),10,0,0);
    forge::domain::RevolveFeature feature("Revolve001",360);
    const auto full=feature.rebuild({circle}); ASSERT_FALSE(full.IsNull());
    EXPECT_EQ(full.ShapeType(),TopAbs_SOLID);
    EXPECT_NEAR(volume(full),80*std::numbers::pi*std::numbers::pi,1e-6);
    feature.setParameter("angle",180.0);
    EXPECT_NEAR(volume(feature.rebuild({circle})),40*std::numbers::pi*std::numbers::pi,1e-6);
    feature.setParameter("axis",0.0);
    const auto xCircle=ShapeFactory::translate(ShapeFactory::makeCircleWire(2),0,10,0);
    EXPECT_NEAR(volume(feature.rebuild({xCircle})),40*std::numbers::pi*std::numbers::pi,1e-6);
    forge::domain::RevolveFeature cylinder("Revolve002",360);
    EXPECT_NEAR(volume(cylinder.rebuild({ShapeFactory::makeRectangleWire(10,20)})),2000*std::numbers::pi,1e-6);
    EXPECT_TRUE(feature.rebuild().IsNull());
    EXPECT_TRUE(feature.rebuild({circle,circle}).IsNull());
    EXPECT_TRUE(feature.rebuild({ShapeFactory::makeBox(1,1,1)}).IsNull());
    feature.setParameter("axis",0.5); EXPECT_FALSE(feature.validate().empty());
    EXPECT_TRUE(feature.rebuild({circle}).IsNull());
    feature.setParameter("axis",1.0); feature.setParameter("angle",361.0);
    EXPECT_FALSE(feature.validate().empty());
    EXPECT_THROW(feature.setParameter("radius",2.0),std::invalid_argument);
}
TEST(RevolveTest, UpstreamEditsVisibilityDeletionAndHistory)
{
    ModelDocument document;
    document.createFeature("CircleSketch",{{"radius",2},{"x",10}});
    document.createRevolveFeature("CircleSketch001",360);
    const auto mass=[&]() { return volume(document.rebuildReport().at("Revolve001").shape); };
    EXPECT_EQ(document.visibleFeatureIds(),(std::vector<std::string>{"Revolve001"}));
    EXPECT_NEAR(mass(),80*std::numbers::pi*std::numbers::pi,1e-6);
    document.setParameter("Revolve001","angle",180);
    EXPECT_NEAR(mass(),40*std::numbers::pi*std::numbers::pi,1e-6);
    document.undo(); EXPECT_NEAR(mass(),80*std::numbers::pi*std::numbers::pi,1e-6);
    document.redo(); EXPECT_NEAR(mass(),40*std::numbers::pi*std::numbers::pi,1e-6);
    document.setParameter("CircleSketch001","radius",3);
    EXPECT_NEAR(mass(),90*std::numbers::pi*std::numbers::pi,1e-6);
    document.deleteFeature("CircleSketch001"); EXPECT_TRUE(document.features().empty());
    document.undo(); EXPECT_NEAR(mass(),90*std::numbers::pi*std::numbers::pi,1e-6);
    EXPECT_THROW(document.addDependency("Revolve001","CircleSketch001"),std::invalid_argument);
}
TEST(RevolveTest, NativeRoundTripAndInvalidCreationPreserveDefinitionsAndRedo)
{
    ModelDocument document;
    document.createFeature("CircleSketch",{{"radius",2},{"x",10}});
    document.createRevolveFeature("CircleSketch001",180);
    document.undo(); ASSERT_TRUE(document.canRedo());
    EXPECT_THROW(document.createRevolveFeature("missing",360),std::invalid_argument);
    for (const double angle : {0.0,361.0,std::numeric_limits<double>::infinity()})
        EXPECT_THROW(document.createRevolveFeature("CircleSketch001",angle),std::invalid_argument);
    EXPECT_THROW(document.createRevolveFeature("CircleSketch001",360,static_cast<RevolveAxis>(9)),std::invalid_argument);
    EXPECT_THROW(document.createRevolveFeature("CircleSketch001",360,RevolveAxis::X),std::invalid_argument);
    EXPECT_TRUE(document.canRedo()); document.redo();
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid()); const auto path=dir.filePath("ring.forgecad");
    forge::infrastructure::NativeDocumentIO::save(document.exportData(),path);
    auto restored=forge::infrastructure::NativeDocumentIO::prepare(path);
    EXPECT_NEAR(volume(restored->rebuildReport().at("Revolve001").shape),40*std::numbers::pi*std::numbers::pi,1e-6);
    restored->setParameter("Revolve001","angle",360);
    EXPECT_NEAR(volume(restored->rebuildReport().at("Revolve001").shape),80*std::numbers::pi*std::numbers::pi,1e-6);
    auto bad=document.exportData(); bad.features[1].parameters["axis"]=0.5;
    EXPECT_THROW(restored->replaceData(bad),std::invalid_argument);
    bad=document.exportData(); bad.features[1].dependencies.clear();
    EXPECT_THROW(restored->replaceData(bad),std::invalid_argument);
}

TEST(RevolveTest, DiagnosticsExplainInvalidAngleInputAndCrossAxis)
{
    forge::domain::RevolveFeature feature("Revolve001",0.0005);
    EXPECT_FALSE(feature.validate().empty());
    feature.setParameter("angle",360.0);
    EXPECT_TRUE(feature.rebuildResult().message.find("一个")!=std::string::npos);
    const auto crossing=feature.rebuildResult({ShapeFactory::makeCircleWire(2)});
    EXPECT_EQ(crossing.status,forge::core::RebuildStatus::Failed);
    EXPECT_TRUE(crossing.message.find("世界 Y")!=std::string::npos);
    const auto wrong=feature.rebuildResult({ShapeFactory::makeBox(1,1,1)});
    EXPECT_TRUE(wrong.message.find("闭合")!=std::string::npos);
    BRepBuilderAPI_MakePolygon polygon;
    polygon.Add(gp_Pnt(10,0,0)); polygon.Add(gp_Pnt(12,0,0)); polygon.Add(gp_Pnt(12,2,0));
    EXPECT_EQ(feature.rebuildResult({polygon.Wire()}).status,forge::core::RebuildStatus::Failed);
    feature.setParameter("angle",0.001);
    EXPECT_EQ(feature.rebuildResult({ShapeFactory::translate(ShapeFactory::makeCircleWire(2),10,0,0)}).status,forge::core::RebuildStatus::Ready);
}

TEST(RevolveTest, NegativeSideAndCreationUndoRestoreDependencies)
{
    ModelDocument document;
    document.createFeature("CircleSketch",{{"radius",2},{"x",-10}});
    document.createRevolveFeature("CircleSketch001",360);
    EXPECT_NEAR(volume(document.rebuildReport().at("Revolve001").shape),80*std::numbers::pi*std::numbers::pi,1e-6);
    document.undo();
    EXPECT_EQ(document.visibleFeatureIds(),(std::vector<std::string>{"CircleSketch001"}));
    EXPECT_EQ(document.findFeature("Revolve001"),nullptr);
    document.redo();
    EXPECT_EQ(document.dependenciesOf("Revolve001"),(std::vector<std::string>{"CircleSketch001"}));
    document.setParameter("CircleSketch001","x",0);
    EXPECT_EQ(document.rebuildReport().at("Revolve001").status,forge::core::RebuildStatus::Failed);
    document.undo();
    EXPECT_EQ(document.rebuildReport().at("Revolve001").status,forge::core::RebuildStatus::Ready);
}
