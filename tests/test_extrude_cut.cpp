#include <gtest/gtest.h>
#include "application/ModelDocument.h"
#include "domain/ExtrudeCutFeature.h"
#include "geometry/ShapeFactory.h"
#include "infrastructure/NativeDocumentIO.h"
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <QTemporaryDir>
#include <numbers>
using forge::application::ModelDocument;
using forge::domain::ExtrudeDirection;
using forge::geometry::ShapeFactory;
namespace {
double mass(const ModelDocument& document) {
    const auto result=document.rebuildReport().at("ExtrudeCut001");
    EXPECT_EQ(result.status,forge::core::RebuildStatus::Ready);
    EXPECT_TRUE(BRepCheck_Analyzer(result.shape).IsValid());
    GProp_GProps props; BRepGProp::VolumeProperties(result.shape,props); return props.Mass();
}
void setup(ModelDocument& document) {
    document.createFeature("Box",{{"length",20},{"width",20},{"height",10}});
    document.createFeature("CircleSketch",{{"radius",2},{"x",10},{"y",10}});
}
}
TEST(ExtrudeCutTest, ThroughHoleBlindHoleDirectionsAndHistory)
{
    ModelDocument document; setup(document);
    document.createExtrudeCutFeature("Box001","CircleSketch001",10);
    EXPECT_NEAR(mass(document),4000-40*std::numbers::pi,1e-6);
    EXPECT_EQ(document.visibleFeatureIds(),(std::vector<std::string>{"ExtrudeCut001"}));
    document.setParameter("ExtrudeCut001","height",5);
    EXPECT_NEAR(mass(document),4000-20*std::numbers::pi,1e-6);
    document.undo(); EXPECT_NEAR(mass(document),4000-40*std::numbers::pi,1e-6);
    document.redo(); EXPECT_NEAR(mass(document),4000-20*std::numbers::pi,1e-6);
    document.setParameter("CircleSketch001","radius",3);
    EXPECT_NEAR(mass(document),4000-45*std::numbers::pi,1e-6);
    document.setParameter("ExtrudeCut001","direction",2);
    EXPECT_NEAR(mass(document),4000-22.5*std::numbers::pi,1e-6);
    document.setParameter("ExtrudeCut001","direction",1);
    EXPECT_EQ(document.rebuildReport().at("ExtrudeCut001").status,forge::core::RebuildStatus::Failed);
    EXPECT_EQ(document.visibleFeatureIds(),(std::vector<std::string>{"Box001","CircleSketch001"}));
    document.undo(); EXPECT_NEAR(mass(document),4000-22.5*std::numbers::pi,1e-6);
    document.deleteFeature("CircleSketch001");
    EXPECT_EQ(document.features().size(),1u);
    document.undo(); EXPECT_NEAR(mass(document),4000-22.5*std::numbers::pi,1e-6);
    EXPECT_THROW(document.addDependency("ExtrudeCut001","Box001"),std::invalid_argument);
}
TEST(ExtrudeCutTest, InvalidCreationPreservesHistoryAndNumbering)
{
    ModelDocument document; setup(document);
    document.setParameter("CircleSketch001","x",100); document.undo();
    ASSERT_TRUE(document.canRedo());
    EXPECT_THROW(document.createExtrudeCutFeature("Box001","CircleSketch001",10,ExtrudeDirection::Reverse),std::invalid_argument);
    EXPECT_THROW(document.createExtrudeCutFeature("CircleSketch001","CircleSketch001",10),std::invalid_argument);
    EXPECT_THROW(document.createExtrudeCutFeature("missing","CircleSketch001",10),std::invalid_argument);
    EXPECT_THROW(document.createExtrudeCutFeature("Box001","CircleSketch001",0),std::invalid_argument);
    EXPECT_THROW(document.createExtrudeCutFeature("Box001","CircleSketch001",10,static_cast<ExtrudeDirection>(9)),std::invalid_argument);
    EXPECT_TRUE(document.canRedo());
    document.redo();
    EXPECT_THROW(document.createExtrudeCutFeature("Box001","CircleSketch001",10),std::invalid_argument);
    document.undo();
    EXPECT_EQ(document.createExtrudeCutFeature("Box001","CircleSketch001",10).id(),"ExtrudeCut001");
}
TEST(ExtrudeCutTest, NativeRoundTripAndSubsequentCutPreserveInputRoles)
{
    ModelDocument document; setup(document);
    document.createExtrudeCutFeature("Box001","CircleSketch001",10,ExtrudeDirection::Symmetric);
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid());
    const auto path=dir.filePath("hole.forgecad");
    forge::infrastructure::NativeDocumentIO::save(document.exportData(),path);
    auto restored=forge::infrastructure::NativeDocumentIO::prepare(path);
    EXPECT_NEAR(mass(*restored),4000-20*std::numbers::pi,1e-6);
    restored->setParameter("ExtrudeCut001","direction",0);
    EXPECT_NEAR(mass(*restored),4000-40*std::numbers::pi,1e-6);
    restored->createFeature("CircleSketch",{{"radius",2},{"x",5},{"y",5}});
    restored->createExtrudeCutFeature("ExtrudeCut001","CircleSketch002",10);
    EXPECT_EQ(restored->rebuildReport().at("ExtrudeCut002").status,forge::core::RebuildStatus::Ready);
    auto bad=document.exportData(); std::swap(bad.features[2].dependencies[0],bad.features[2].dependencies[1]);
    EXPECT_THROW(restored->replaceData(bad),std::invalid_argument);
}
TEST(ExtrudeCutTest, EntireRemovalIsEmptyAndNonSolidInputsAreRejected)
{
    forge::domain::ExtrudeCutFeature feature("ExtrudeCut001",10);
    const auto box=ShapeFactory::makeBox(10,10,10);
    const auto wire=ShapeFactory::makeRectangleWire(10,10);
    EXPECT_EQ(feature.rebuildResult({box,wire}).status,forge::core::RebuildStatus::Empty);
    EXPECT_EQ(feature.rebuildResult({wire,wire}).status,forge::core::RebuildStatus::Failed);
    EXPECT_EQ(feature.rebuildResult({box}).status,forge::core::RebuildStatus::Failed);
    EXPECT_EQ(feature.rebuildResult({box,box}).status,forge::core::RebuildStatus::Failed);
    ModelDocument document;
    document.createFeature("Box",{{"length",10},{"width",10},{"height",10}});
    document.createFeature("RectangleSketch",{{"length",10},{"width",10}});
    document.createExtrudeCutFeature("Box001","RectangleSketch001",10);
    EXPECT_EQ(document.visibleFeatureIds(),(std::vector<std::string>{"ExtrudeCut001"}));
    EXPECT_EQ(document.shapeForExport().status,forge::core::RebuildStatus::Empty);
}
