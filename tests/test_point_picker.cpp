#include <gtest/gtest.h>
#include "geometry/PointPicker.h"
#include "geometry/ShapeFactory.h"
#include "application/ModelDocument.h"
#include "infrastructure/NativeDocumentIO.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QTemporaryDir>
#include <limits>
#include <numbers>
using forge::geometry::pickPoint;
using forge::geometry::ShapeFactory;
using forge::application::ModelDocument;

TEST(PointPickerTest, UsesNearestExactSurfaceOrCurrentWorkPlane)
{
    const auto box=ShapeFactory::makeBox(20,20,10);
    const auto far=ShapeFactory::translate(box,0,0,-30);
    const auto point=pickPoint(gp_Pnt(10,10,100),gp_Dir(0,0,-1),{far,box},0);
    ASSERT_TRUE(point); EXPECT_TRUE(point->onSurface);
    EXPECT_NEAR(point->point.X(),10,1e-8);
    EXPECT_NEAR(point->point.Y(),10,1e-8);
    EXPECT_NEAR(point->point.Z(),10,1e-8);
    const auto plane=pickPoint(gp_Pnt(30,30,100),gp_Dir(0,0,-1),{box},7);
    ASSERT_TRUE(plane); EXPECT_FALSE(plane->onSurface);
    EXPECT_NEAR(plane->point.Z(),7,1e-8);
    EXPECT_FALSE(pickPoint(gp_Pnt(30,30,100),gp_Dir(1,0,0),{box},0));
    EXPECT_FALSE(pickPoint(gp_Pnt(30,30,100),gp_Dir(0,0,1),{box},0));
    EXPECT_FALSE(pickPoint(gp_Pnt(0,0,100),gp_Dir(0,0,-1),{},std::numeric_limits<double>::infinity()));
    const auto sphere=pickPoint(gp_Pnt(0,0,100),gp_Dir(0,0,-1),{ShapeFactory::makeSphere(10)},0);
    ASSERT_TRUE(sphere); EXPECT_TRUE(sphere->onSurface); EXPECT_NEAR(sphere->point.Z(),10,1e-8);
}

TEST(PointPlacementTest, XYZIsOneUndoAndInvalidOrNoOpPlacementPreservesRedo)
{
    ModelDocument document;
    document.createFeature("CircleSketch",{{"radius",2}});
    document.markSaved();
    document.setPosition("CircleSketch001",10,12,20);
    const auto position=[&]() {
        const auto* feature=document.findFeature("CircleSketch001");
        return std::vector<double>{feature->parameters()[1].asDouble(),feature->parameters()[2].asDouble(),feature->parameters()[3].asDouble()};
    };
    EXPECT_EQ(position(),(std::vector<double>{10,12,20}));
    document.undo(); EXPECT_EQ(position(),(std::vector<double>{0,0,0}));
    EXPECT_FALSE(document.isModified()); EXPECT_TRUE(document.canRedo());
    document.setPosition("CircleSketch001",0,0,0); EXPECT_TRUE(document.canRedo());
    EXPECT_THROW(document.setPosition("CircleSketch001",1,2,1e7),std::invalid_argument);
    EXPECT_THROW(document.setPosition("CircleSketch001",1,std::numeric_limits<double>::quiet_NaN(),3),std::invalid_argument);
    EXPECT_TRUE(document.canRedo()); EXPECT_EQ(position(),(std::vector<double>{0,0,0}));
    document.redo(); EXPECT_EQ(position(),(std::vector<double>{10,12,20}));
}

TEST(PointPlacementTest, SketchOnTopFaceMakesReverseBlindHoleAndSurvivesNativeRoundTrip)
{
    ModelDocument document;
    document.createFeature("Box",{{"length",20},{"width",20},{"height",10}});
    document.createFeature("CircleSketch",{{"radius",2}});
    const auto hit=pickPoint(gp_Pnt(10,10,100),gp_Dir(0,0,-1),{document.rebuildReport().at("Box001").shape},0);
    ASSERT_TRUE(hit);
    document.setPosition("CircleSketch001",hit->point.X(),hit->point.Y(),hit->point.Z());
    document.createExtrudeCutFeature("Box001","CircleSketch001",5,forge::domain::ExtrudeDirection::Reverse);
    const auto result=document.rebuildReport().at("ExtrudeCut001");
    ASSERT_EQ(result.status,forge::core::RebuildStatus::Ready);
    GProp_GProps properties; BRepGProp::VolumeProperties(result.shape,properties);
    EXPECT_NEAR(properties.Mass(),4000-20*std::numbers::pi,1e-6);
    const auto bottom=pickPoint(gp_Pnt(10,10,-100),gp_Dir(0,0,1),{result.shape},0);
    ASSERT_TRUE(bottom); EXPECT_NEAR(bottom->point.Z(),0,1e-8);
    const auto top=pickPoint(gp_Pnt(10,10,100),gp_Dir(0,0,-1),{result.shape},0);
    ASSERT_TRUE(top); EXPECT_NEAR(top->point.Z(),5,1e-8);
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("placed.forgecad");
    forge::infrastructure::NativeDocumentIO::save(document.exportData(),path);
    auto restored=forge::infrastructure::NativeDocumentIO::prepare(path);
    EXPECT_DOUBLE_EQ(restored->findFeature("CircleSketch001")->parameters()[3].asDouble(),10);
    EXPECT_EQ(restored->rebuildReport().at("ExtrudeCut001").status,forge::core::RebuildStatus::Ready);
    document.createFeature("RectangleSketch",{{"length",3},{"width",4},{"z",8}});
    GProp_GProps wire; BRepGProp::LinearProperties(document.rebuildReport().at("RectangleSketch001").shape,wire);
    EXPECT_NEAR(wire.CentreOfMass().Z(),8,1e-8);
}

TEST(PointPlacementTest, LegacySketchFilesDefaultToZeroHeight)
{
    ModelDocument document;
    document.createFeature("CircleSketch",{{"radius",2}});
    document.createFeature("RectangleSketch",{{"length",3},{"width",4}});
    auto data=document.exportData();
    for (auto& feature : data.features) feature.parameters.erase("z");
    ModelDocument restored; restored.replaceData(data);
    EXPECT_DOUBLE_EQ(restored.findFeature("CircleSketch001")->parameters()[3].asDouble(),0);
    EXPECT_DOUBLE_EQ(restored.findFeature("RectangleSketch001")->parameters()[4].asDouble(),0);
    auto malformed=data; malformed.features[0].parameters.erase("x");
    EXPECT_THROW(restored.replaceData(malformed),std::invalid_argument);
}
