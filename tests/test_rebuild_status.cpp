#include <gtest/gtest.h>
#include "application/ModelDocument.h"
#include "domain/BooleanFeature.h"
#include "domain/Feature.h"
#include "geometry/ShapeFactory.h"
#include "assistant/ToolRegistry.h"
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <GProp_GProps.hxx>

using forge::application::ModelDocument;
using forge::core::RebuildStatus;
using forge::domain::BooleanOperation;
using forge::geometry::ShapeFactory;
namespace {
void boxes(ModelDocument& d)
{
    d.createFeature("Box", {{"length", 10}, {"width", 10}, {"height", 10}});
    d.createFeature("Box", {{"length", 5}, {"width", 5}, {"height", 5}, {"x", 20}});
}
double volume(const TopoDS_Shape& shape)
{
    GProp_GProps p;
    BRepGProp::VolumeProperties(shape, p);
    return p.Mass();
}
}

TEST(RebuildStatusTest, EmptyIntersectionRecoversThroughEditUndoRedo)
{
    ModelDocument d;
    boxes(d);
    d.createBooleanFeature(BooleanOperation::Intersection, "Box001", "Box002");
    auto report = d.rebuildReport();
    EXPECT_EQ(report.at("Intersection001").status, RebuildStatus::Empty);
    EXPECT_TRUE(report.at("Intersection001").usable());
    EXPECT_FALSE(report.at("Intersection001").shape.IsNull());
    EXPECT_FALSE(report.at("Intersection001").message.empty());
    EXPECT_EQ(d.visibleFeatureIds(report), (std::vector<std::string>{"Intersection001"}));
    d.setParameter("Box002", "x", 2);
    EXPECT_EQ(d.rebuildReport().at("Intersection001").status, RebuildStatus::Ready);
    d.undo();
    EXPECT_EQ(d.rebuildReport().at("Intersection001").status, RebuildStatus::Empty);
    d.redo();
    EXPECT_EQ(d.rebuildReport().at("Intersection001").status, RebuildStatus::Ready);
}

TEST(RebuildStatusTest, FullyRemovedDifferenceIsLegitimateEmptyResult)
{
    const auto shape = ShapeFactory::makeBox(10, 10, 10);
    auto result = ShapeFactory::differenceResult(shape, shape);
    EXPECT_EQ(result.status, RebuildStatus::Empty);
    EXPECT_TRUE(result.usable());
    EXPECT_NE(result.message.find("完全减去"), std::string::npos);
}

TEST(RebuildStatusTest, EmptyInputsFollowSetIdentities)
{
    const auto first = ShapeFactory::makeBox(10, 10, 10);
    const auto second = ShapeFactory::translate(first, 20, 0, 0);
    const auto empty = ShapeFactory::intersectionResult(first, second).shape;
    EXPECT_EQ(ShapeFactory::unionResult(empty, first).status, RebuildStatus::Ready);
    EXPECT_NEAR(volume(ShapeFactory::unionResult(first, empty).shape), 1000, 1e-5);
    EXPECT_EQ(ShapeFactory::intersectionResult(empty, first).status, RebuildStatus::Empty);
    EXPECT_EQ(ShapeFactory::differenceResult(empty, first).status, RebuildStatus::Empty);
    EXPECT_NEAR(volume(ShapeFactory::differenceResult(first, empty).shape), 1000, 1e-5);
    EXPECT_EQ(ShapeFactory::unionResult(empty, empty).status, RebuildStatus::Empty);
    // 空结果不应该掩盖另一个无效输入。
    EXPECT_EQ(ShapeFactory::intersectionResult(empty, {}).status, RebuildStatus::Failed);
}

TEST(RebuildStatusTest, NullAndInvalidTopologyCarryReasons)
{
    auto result = ShapeFactory::differenceResult({}, ShapeFactory::makeBox(1, 1, 1));
    EXPECT_EQ(result.status, RebuildStatus::Failed);
    EXPECT_FALSE(result.message.empty());
    BRep_Builder builder;
    TopoDS_Edge broken;
    builder.MakeEdge(broken); // 无曲线、无顶点的损坏拓扑，不能当成合法空集合。
    result = ShapeFactory::inspectShape(broken);
    EXPECT_EQ(result.status, RebuildStatus::Failed);
    EXPECT_NE(result.message.find("拓扑"), std::string::npos);
    forge::domain::BooleanFeature feature("Cut", BooleanOperation::Difference);
    EXPECT_EQ(feature.rebuildResult({}).status, RebuildStatus::Failed);
}

TEST(RebuildStatusTest, FailedInputBlocksDownstreamAndExposesHealthyInputs)
{
    ModelDocument d;
    boxes(d);
    d.createFeature("Sphere", {{"radius", 3}});
    d.createBooleanFeature(BooleanOperation::Difference, "Box001", "Box002");
    d.createBooleanFeature(BooleanOperation::Union, "Cut001", "Sphere001");
    // 故障注入模拟损坏的上游参数，不通过生产 UI 的参数校验。
    d.findFeature("Box002")->setParameter("length", -1.0);
    auto report = d.rebuildReport();
    EXPECT_EQ(report.at("Box002").status, RebuildStatus::Failed);
    EXPECT_EQ(report.at("Cut001").status, RebuildStatus::Blocked);
    EXPECT_EQ(report.at("Union001").status, RebuildStatus::Blocked);
    EXPECT_NE(report.at("Cut001").message.find("Box002"), std::string::npos);
    EXPECT_EQ(report.at("Sphere001").status, RebuildStatus::Ready);
    EXPECT_EQ(d.visibleFeatureIds(report), (std::vector<std::string>{"Box001", "Sphere001"}));
    d.findFeature("Box002")->setParameter("length", 5.0);
    report = d.rebuildReport();
    EXPECT_EQ(report.at("Union001").status, RebuildStatus::Ready);
    EXPECT_EQ(d.visibleFeatureIds(report), (std::vector<std::string>{"Union001"}));
}

TEST(RebuildStatusTest, EmptyIntermediateIsUsableByDownstream)
{
    ModelDocument d;
    boxes(d);
    d.createFeature("Sphere", {{"radius", 3}});
    d.createBooleanFeature(BooleanOperation::Intersection, "Box001", "Box002");
    d.createBooleanFeature(BooleanOperation::Union, "Intersection001", "Sphere001");
    EXPECT_EQ(d.rebuildReport().at("Union001").status, RebuildStatus::Ready);
    d.deleteFeature("Box001");
    d.undo();
    EXPECT_EQ(d.rebuildReport().at("Intersection001").status, RebuildStatus::Empty);
    EXPECT_EQ(d.rebuildReport().at("Union001").status, RebuildStatus::Ready);
}

TEST(RebuildStatusTest, AiQueriesReportGeometryFailureSeparatelyFromDataSuccess)
{
    ModelDocument d;
    boxes(d);
    d.createBooleanFeature(BooleanOperation::Intersection, "Box001", "Box002");
    forge::assistant::ToolRegistry tools(d);
    auto result = tools.execute("get_feature", {{"feature_id", "Intersection001"}});
    EXPECT_TRUE(result["success"].toBool());
    EXPECT_EQ(result["rebuild_status"].toString(), "empty");
    d.findFeature("Box002")->setParameter("length", -1.0);
    result = tools.execute("get_feature", {{"feature_id", "Intersection001"}});
    EXPECT_EQ(result["rebuild_status"].toString(), "blocked");
    EXPECT_TRUE(result["rebuild_message"].toString().contains("Box002"));
}
