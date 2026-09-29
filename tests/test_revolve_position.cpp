#include <gtest/gtest.h>
#include "application/ModelDocument.h"
#include "assistant/ToolRegistry.h"
#include "domain/BooleanFeature.h"
#include "domain/Feature.h"
#include "domain/RevolveFeature.h"
#include "geometry/ShapeAnalyzer.h"
#include "geometry/ShapeFactory.h"
#include "infrastructure/NativeDocumentIO.h"
#include <QTemporaryDir>
#include <limits>
#include <numbers>

using forge::application::ModelDocument;
using forge::assistant::ToolRegistry;
using forge::geometry::ShapeAnalyzer;
using forge::domain::RevolveAxis;

namespace {
forge::geometry::ShapeAnalysis measure(const ModelDocument& document,const std::string& id="Revolve001")
{
    return ShapeAnalyzer::analyze(document.rebuildReport().at(id).shape);
}
void shiftedBounds(const forge::geometry::ShapeAnalysis& before,const forge::geometry::ShapeAnalysis& after,
    double x,double y,double z)
{
    ASSERT_TRUE(before.bounds); ASSERT_TRUE(after.bounds);
    const double offset[]{x,y,z};
    for (int i=0;i<6;++i) EXPECT_NEAR((*after.bounds)[i],(*before.bounds)[i]+offset[i%3],1e-6);
    ASSERT_TRUE(before.volume); ASSERT_TRUE(after.volume);
    EXPECT_NEAR(*after.volume,*before.volume,1e-5);
}
}

TEST(RevolvePositionTest, BothAxesAndHalfRingsTranslateOnceAndXYZIsOneHistoryEntry)
{
    for (const auto axis : {RevolveAxis::X,RevolveAxis::Y}) for (const double angle : {180.0,360.0}) {
        ModelDocument document; ToolRegistry tools(document);
        document.createFeature("CircleSketch",{{"radius",2},{"x",axis==RevolveAxis::X ? 0.0 : 10.0},
            {"y",axis==RevolveAxis::X ? 10.0 : 0.0}});
        document.createRevolveFeature("CircleSketch001",angle,axis);
        const auto initial=measure(document);
        document.markSaved();
        const auto result=tools.execute("set_position",{{"feature_id","Revolve001"},{"x",13},{"y",-8},{"z",120}});
        ASSERT_TRUE(result.value("success").toBool()); EXPECT_TRUE(result.value("supports_position").toBool());
        shiftedBounds(initial,measure(document),13,-8,120);
        // Setting the same offset again is a no-op, not cumulative displacement/history.
        document.setPosition("Revolve001",13,-8,120);
        shiftedBounds(initial,measure(document),13,-8,120);
        document.undo(); shiftedBounds(initial,measure(document),0,0,0);
        EXPECT_FALSE(document.isModified());
        document.redo(); shiftedBounds(initial,measure(document),13,-8,120);
        document.setPosition("Revolve001",20,-10,100);
        shiftedBounds(initial,measure(document),20,-10,100);
        document.undo(); shiftedBounds(initial,measure(document),13,-8,120);
        const auto sketch=tools.execute("get_feature",{{"feature_id","CircleSketch001"}}).value("parameters").toObject();
        EXPECT_DOUBLE_EQ(sketch.value("z").toDouble(),0);
        EXPECT_DOUBLE_EQ(sketch.value("x").toDouble(),axis==RevolveAxis::X ? 0 : 10);
    }
}

TEST(RevolvePositionTest, MovingRingRebuildsBooleanConnectionAndRetainsOffsetDuringEdits)
{
    ModelDocument document; ToolRegistry tools(document);
    document.createFeature("CircleSketch",{{"radius",2},{"x",10}});
    document.createRevolveFeature("CircleSketch001",360);
    document.createFeature("Sphere",{{"radius",10},{"z",120}});
    document.createBooleanFeature(forge::domain::BooleanOperation::Union,"Sphere001","Revolve001");
    EXPECT_EQ(measure(document,"Union001").solids,2);
    document.setPosition("Revolve001",20,0,120);
    EXPECT_EQ(measure(document,"Union001").solids,1);
    document.undo(); EXPECT_EQ(measure(document,"Union001").solids,2);
    document.redo(); EXPECT_EQ(measure(document,"Union001").solids,1);
    document.setParameter("Revolve001","angle",180);
    EXPECT_NEAR(*measure(document).volume,40*std::numbers::pi*std::numbers::pi,1e-5);
    EXPECT_EQ(measure(document,"Union001").solids,1);
    document.setParameter("CircleSketch001","radius",3);
    EXPECT_NEAR(*measure(document).volume,90*std::numbers::pi*std::numbers::pi,1e-5);
    const auto feature=tools.execute("get_feature",{{"feature_id","Revolve001"}}).value("parameters").toObject();
    EXPECT_DOUBLE_EQ(feature.value("x").toDouble(),20);
    EXPECT_DOUBLE_EQ(feature.value("z").toDouble(),120);
    document.setParameter("CircleSketch001","x",0);
    EXPECT_EQ(document.rebuildReport().at("Union001").status,forge::core::RebuildStatus::Blocked);
    document.undo(); EXPECT_EQ(measure(document,"Union001").solids,1);
    EXPECT_EQ(document.dependenciesOf("Revolve001"),(std::vector<std::string>{"CircleSketch001"}));
}

TEST(RevolvePositionTest, NativeRoundTripPreservesOffsetAndOldFilesDefaultToZero)
{
    ModelDocument document; ToolRegistry tools(document);
    document.createFeature("CircleSketch",{{"radius",2},{"x",10}});
    document.createRevolveFeature("CircleSketch001",180);
    document.setPosition("Revolve001",-35,7,120);
    QTemporaryDir dir; ASSERT_TRUE(dir.isValid()); const auto path=dir.filePath("moved.forgecad");
    forge::infrastructure::NativeDocumentIO::save(document.exportData(),path);
    auto restored=forge::infrastructure::NativeDocumentIO::prepare(path);
    shiftedBounds(measure(document),measure(*restored),0,0,0);
    auto legacy=document.exportData();
    for (const auto* name : {"x","y","z"}) legacy.features[1].parameters.erase(name);
    const auto legacyPath=dir.filePath("legacy.forgecad");
    forge::infrastructure::NativeDocumentIO::save(legacy,legacyPath);
    auto old=forge::infrastructure::NativeDocumentIO::prepare(legacyPath);
    shiftedBounds(measure(*old),measure(*restored),-35,7,120);
    EXPECT_EQ(old->exportData().features[1].parameters.at("z"),0);
    restored->setParameter("CircleSketch001","radius",3);
    const auto reloaded=restored->exportData();
    EXPECT_DOUBLE_EQ(reloaded.features[1].parameters.at("x"),-35);
    EXPECT_DOUBLE_EQ(reloaded.features[1].parameters.at("z"),120);
    restored->undo(); shiftedBounds(measure(document),measure(*restored),0,0,0);
}

TEST(RevolvePositionTest, InvalidOffsetsAndNativeFieldsPreserveRedoAndCurrentDocument)
{
    ModelDocument document;
    document.createFeature("CircleSketch",{{"radius",2},{"x",10}});
    document.createRevolveFeature("CircleSketch001",360);
    document.setPosition("Revolve001",1,2,3); document.undo();
    const auto initial=measure(document);
    for (double invalid : {1000001.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        EXPECT_THROW(document.setPosition("Revolve001",0,0,invalid),std::invalid_argument);
        EXPECT_THROW(document.setParameter("Revolve001","z",invalid),std::invalid_argument);
        auto data=document.exportData(); data.features[1].parameters["z"]=invalid;
        EXPECT_THROW(document.replaceData(data),std::invalid_argument);
        EXPECT_TRUE(document.canRedo()); shiftedBounds(initial,measure(document),0,0,0);
        forge::domain::RevolveFeature direct("Revolve001",360,RevolveAxis::Y,0,0,invalid);
        EXPECT_FALSE(direct.validate().empty());
        EXPECT_EQ(direct.rebuildResult({forge::geometry::ShapeFactory::translate(
            forge::geometry::ShapeFactory::makeCircleWire(2),10,0,0)}).status,forge::core::RebuildStatus::Failed);
    }
    auto unknown=document.exportData(); unknown.features[1].parameters["offsetZ"]=7;
    EXPECT_THROW(document.replaceData(unknown),std::invalid_argument);
    EXPECT_TRUE(document.canRedo()); document.redo(); shiftedBounds(initial,measure(document),1,2,3);
}
