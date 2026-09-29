#include <gtest/gtest.h>
#include "application/ModelDocument.h"
#include "assistant/ToolRegistry.h"
#include "domain/BooleanFeature.h"
#include "domain/Feature.h"
#include "geometry/ShapeAnalyzer.h"
#include "geometry/ShapeFactory.h"
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <numbers>

using forge::application::ModelDocument;
using forge::assistant::ToolRegistry;
using forge::domain::BooleanOperation;

namespace {
QJsonObject analyze(ToolRegistry& tools,const QString& id={})
{
    const auto result=tools.execute("analyze_geometry",id.isEmpty() ? QJsonObject{} : QJsonObject{{"feature_id",id}});
    EXPECT_TRUE(result.value("success").toBool()) << result.value("error").toString().toStdString();
    return result;
}
void xyz(const QJsonObject& point,double x,double y,double z)
{
    EXPECT_NEAR(point.value("x").toDouble(),x,1e-6);
    EXPECT_NEAR(point.value("y").toDouble(),y,1e-6);
    EXPECT_NEAR(point.value("z").toDouble(),z,1e-6);
}
}

TEST(GeometryToolsTest, MeasuresWorldBoxAndPreservesSavedStateAndRedo)
{
    ModelDocument document; ToolRegistry tools(document);
    document.createFeature("Box",{{"length",10},{"width",20},{"height",30},{"x",-7},{"y",4},{"z",100}});
    document.markSaved();
    document.createFeature("Sphere",{{"radius",2}}); document.undo();
    ASSERT_FALSE(document.isModified()); ASSERT_TRUE(document.canRedo());
    const auto result=analyze(tools);
    EXPECT_EQ(result.value("feature_ids").toArray(),(QJsonArray{"Box001"}));
    const auto geometry=result.value("geometry").toObject();
    EXPECT_TRUE(geometry.value("valid").toBool());
    EXPECT_TRUE(geometry.value("single_solid").toBool());
    EXPECT_EQ(geometry.value("solid_count").toInt(),1);
    const auto bounds=geometry.value("bounds_mm").toObject();
    xyz(bounds.value("min").toObject(),-7,4,100);
    xyz(bounds.value("max").toObject(),3,24,130);
    xyz(bounds.value("size").toObject(),10,20,30);
    xyz(geometry.value("volume_centroid_mm").toObject(),-2,14,115);
    EXPECT_NEAR(geometry.value("volume_mm3").toDouble(),6000,1e-6);
    EXPECT_NEAR(geometry.value("surface_area_mm2").toDouble(),2200,1e-6);
    const auto topology=geometry.value("topology").toObject();
    EXPECT_EQ(topology.value("faces").toInt(),6);
    EXPECT_EQ(topology.value("edges").toInt(),12);
    EXPECT_EQ(topology.value("vertices").toInt(),8);
    EXPECT_FALSE(result.contains("model_changed"));
    EXPECT_FALSE(document.isModified()); EXPECT_TRUE(document.canRedo());
    document.redo(); EXPECT_NE(document.findFeature("Sphere001"),nullptr);
}

TEST(GeometryToolsTest, RotationExamplesMatchExactGeometryAndResultTranslationMovesBounds)
{
    for (const bool xAxis : {false,true}) {
        ModelDocument document; ToolRegistry tools(document);
        document.createFeature("CircleSketch",{{"radius",2},{"x",xAxis ? 25.0 : 10.0},{"y",xAxis ? 10.0 : 20.0}});
        document.createRevolveFeature("CircleSketch001",360,xAxis ? forge::domain::RevolveAxis::X : forge::domain::RevolveAxis::Y);
        const auto geometry=analyze(tools,"Revolve001").value("geometry").toObject();
        const auto bounds=geometry.value("bounds_mm").toObject();
        if (xAxis) {
            xyz(bounds.value("min").toObject(),23,-12,-12);
            xyz(bounds.value("max").toObject(),27,12,12);
            xyz(geometry.value("volume_centroid_mm").toObject(),25,0,0);
        } else {
            xyz(bounds.value("min").toObject(),-12,18,-12);
            xyz(bounds.value("max").toObject(),12,22,12);
            xyz(geometry.value("volume_centroid_mm").toObject(),0,20,0);
        }
        EXPECT_NEAR(geometry.value("volume_mm3").toDouble(),80*std::numbers::pi*std::numbers::pi,1e-5);
        document.markSaved();
        const auto moved=tools.execute("set_position",{{"feature_id","Revolve001"},{"x",0},{"y",0},{"z",95}});
        EXPECT_TRUE(moved.value("success").toBool()); EXPECT_TRUE(document.isModified());
        const auto translated=analyze(tools,"Revolve001").value("geometry").toObject();
        xyz(translated.value("volume_centroid_mm").toObject(),xAxis ? 25 : 0,xAxis ? 0 : 20,95);
        EXPECT_NEAR(translated.value("volume_mm3").toDouble(),geometry.value("volume_mm3").toDouble(),1e-6);
        document.undo(); EXPECT_FALSE(document.isModified());
        EXPECT_EQ(analyze(tools,"Revolve001").value("geometry").toObject(),geometry);
    }
}

TEST(GeometryToolsTest, FinalDocumentExcludesConsumedBooleanInputs)
{
    ModelDocument document; ToolRegistry tools(document);
    document.createFeature("Box",{{"length",10},{"width",10},{"height",10}});
    document.createFeature("Box",{{"length",5},{"width",5},{"height",5},{"x",2},{"y",2},{"z",2}});
    document.createBooleanFeature(BooleanOperation::Difference,"Box001","Box002");
    const auto result=analyze(tools);
    EXPECT_EQ(result.value("feature_ids").toArray(),(QJsonArray{"Cut001"}));
    EXPECT_NEAR(result.value("geometry").toObject().value("volume_mm3").toDouble(),875,1e-6);
    EXPECT_NEAR(analyze(tools,"Box001").value("geometry").toObject().value("volume_mm3").toDouble(),1000,1e-6);
}

TEST(GeometryToolsTest, TrophyTopSpherePointContactIsTwoSolidsAndOverlapRepairsIt)
{
    ModelDocument document; ToolRegistry tools(document);
    document.createFeature("Cylinder",{{"radius",7},{"height",20},{"z",118}});
    document.createFeature("Sphere",{{"radius",10},{"z",148}}); // Lowest point = cylinder top = 138.
    document.createBooleanFeature(BooleanOperation::Union,"Cylinder001","Sphere001");
    auto geometry=analyze(tools,"Union001").value("geometry").toObject();
    EXPECT_EQ(geometry.value("solid_count").toInt(),2);
    EXPECT_FALSE(geometry.value("single_solid").toBool());
    EXPECT_FALSE(geometry.value("warnings").toArray().isEmpty());
    document.setPosition("Sphere001",0,0,146); // Positive overlap, rather than a tangent point.
    geometry=analyze(tools).value("geometry").toObject();
    EXPECT_EQ(geometry.value("solid_count").toInt(),1);
    EXPECT_TRUE(geometry.value("single_solid").toBool());
}

TEST(GeometryToolsTest, EmptyResultsAndSketchesDoNotInventVolumeOrBounds)
{
    ModelDocument document; ToolRegistry tools(document);
    auto geometry=analyze(tools).value("geometry").toObject();
    EXPECT_TRUE(geometry.value("empty").toBool()); EXPECT_TRUE(geometry.value("bounds_mm").isNull());
    EXPECT_EQ(geometry.value("solid_count").toInt(),0);
    document.createFeature("CircleSketch",{{"radius",2},{"x",10},{"y",20},{"z",30}});
    geometry=analyze(tools).value("geometry").toObject();
    EXPECT_FALSE(geometry.value("empty").toBool()); EXPECT_FALSE(geometry.value("solid_body").toBool());
    EXPECT_TRUE(geometry.value("volume_mm3").isNull()); EXPECT_TRUE(geometry.value("volume_centroid_mm").isNull());
    xyz(geometry.value("bounds_mm").toObject().value("size").toObject(),4,4,0);
    ModelDocument other; ToolRegistry otherTools(other);
    other.createFeature("Box",{{"length",2},{"width",2},{"height",2}});
    other.createFeature("Box",{{"length",2},{"width",2},{"height",2},{"x",10}});
    other.createBooleanFeature(BooleanOperation::Intersection,"Box001","Box002");
    geometry=analyze(otherTools).value("geometry").toObject();
    EXPECT_TRUE(geometry.value("empty").toBool()); EXPECT_TRUE(geometry.value("bounds_mm").isNull());
    EXPECT_EQ(geometry.value("volume_mm3").toDouble(),0);
    other.createFeature("Box",{{"length",3},{"width",3},{"height",3},{"z",10}});
    geometry=analyze(otherTools).value("geometry").toObject();
    EXPECT_TRUE(geometry.value("single_solid").toBool());
    EXPECT_NEAR(geometry.value("volume_mm3").toDouble(),27,1e-6); // Empty final branches do not turn solids into mixed geometry.
}

TEST(GeometryToolsTest, MixedFinalGeometryIsExplicitAndSharedTopologyIsNotDoubleCounted)
{
    ModelDocument document; ToolRegistry tools(document);
    document.createFeature("Box",{{"length",2},{"width",2},{"height",2}});
    document.createFeature("CircleSketch",{{"radius",2},{"x",10}});
    const auto geometry=analyze(tools).value("geometry").toObject();
    EXPECT_EQ(geometry.value("solid_count").toInt(),1);
    EXPECT_FALSE(geometry.value("single_solid").toBool()); EXPECT_TRUE(geometry.value("volume_mm3").isNull());
    const auto box=forge::geometry::ShapeFactory::makeBox(2,3,4);
    TopoDS_Compound compound; BRep_Builder builder; builder.MakeCompound(compound);
    builder.Add(compound,box); builder.Add(compound,box);
    const auto analysis=forge::geometry::ShapeAnalyzer::analyze(compound);
    EXPECT_EQ(analysis.solids,1); EXPECT_EQ(analysis.faces,6);
    ASSERT_TRUE(analysis.volume); EXPECT_NEAR(*analysis.volume,24,1e-6);
}

TEST(GeometryToolsTest, FailedDownstreamIsNotReplacedByVisibleUpstreamDuringAnalysis)
{
    ModelDocument document; ToolRegistry tools(document);
    document.createFeature("CircleSketch",{{"radius",2},{"x",10}});
    document.createRevolveFeature("CircleSketch001",360);
    document.setPosition("CircleSketch001",0,0,0);
    const auto failed=tools.execute("analyze_geometry",{});
    EXPECT_FALSE(failed.value("success").toBool()); EXPECT_FALSE(failed.contains("geometry"));
    ASSERT_EQ(failed.value("failed_features").toArray().size(),1);
    EXPECT_FALSE(tools.execute("analyze_geometry",{{"feature_id","Revolve001"}}).value("success").toBool());
    EXPECT_TRUE(analyze(tools,"CircleSketch001").value("geometry").toObject().value("volume_mm3").isNull());
}

TEST(GeometryToolsTest, CapabilitiesMatchExecutionAndRejectMalformedAnalysisWithoutHistoryChanges)
{
    ModelDocument document; ToolRegistry tools(document);
    const auto types=tools.execute("get_feature_types",{});
    const auto rules=tools.execute("get_spatial_rules",{});
    EXPECT_EQ(types.value("spatial_rules").toObject(),rules);
    for (const auto& item : types.value("types").toArray()) {
        const auto type=item.toObject();
        const auto name=type.value("type").toString();
        EXPECT_EQ(type.value("supports_position").toBool(),rules.value("position_supported_types").toArray().contains(name));
    }
    document.createFeature("Sphere",{{"radius",2}}); document.undo();
    for (const auto& args : {QJsonObject{{"feature_id","missing"}},QJsonObject{{"feature_id",12}},
        QJsonObject{{"feature_id"," "}},QJsonObject{{"scope","all"}}}) {
        EXPECT_FALSE(tools.execute("analyze_geometry",args).value("success").toBool());
        EXPECT_TRUE(document.canRedo()); EXPECT_TRUE(document.features().empty());
    }
    EXPECT_FALSE(tools.execute("get_spatial_rules",{{"axis","Y"}}).value("success").toBool());
    EXPECT_THROW(forge::geometry::ShapeAnalyzer::analyze(TopoDS_Shape{}),std::invalid_argument);
}
