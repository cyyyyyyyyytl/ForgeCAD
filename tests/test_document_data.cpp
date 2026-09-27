#include <gtest/gtest.h>
#include "application/ModelDocument.h"
#include "domain/BooleanFeature.h"
#include "geometry/ShapeFactory.h"
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <sstream>

using forge::application::ModelDocument;

TEST(DocumentDataTest, PreservesParametersOrderAndBooleanInputRoles)
{
    ModelDocument document;
    document.createFeature("Box", {{"length",10},{"width",20},{"height",30},{"x",-4}});
    document.createFeature("Cylinder", {{"radius",2},{"height",40},{"y",3}});
    document.createBooleanFeature(forge::domain::BooleanOperation::Difference,"Box001","Cylinder001");
    const auto data = document.exportData();
    ASSERT_EQ(data.features.size(),3);
    EXPECT_EQ(data.features[0].id,"Box001");
    EXPECT_DOUBLE_EQ(data.features[0].parameters.at("x"),-4);
    EXPECT_EQ(data.features[1].type,"Cylinder");
    EXPECT_EQ(data.features[2].type,"Cut");
    EXPECT_EQ(data.features[2].dependencies,(std::vector<std::string>{"Box001","Cylinder001"}));
    EXPECT_TRUE(data.features[2].parameters.empty());
    EXPECT_TRUE(data.geometryAssets.empty());
    EXPECT_EQ(data.units,"mm");
}

TEST(DocumentDataTest, ExportKeepsRedoAndAllocatedSequences)
{
    ModelDocument document;
    document.createFeature("Box", {{"length",1},{"width",2},{"height",3}});
    document.createFeature("Sphere", {{"radius",2}});
    document.addDependency("Box001","Sphere001");
    document.createFeature("Sphere", {{"radius",3}});
    document.undo();
    ASSERT_TRUE(document.canRedo());
    const auto data = document.exportData();
    EXPECT_EQ(data.sequences.at("Sphere"),2);
    EXPECT_EQ(data.features[0].dependencies,(std::vector<std::string>{"Sphere001"}));
    EXPECT_TRUE(document.canRedo());
    document.redo();
    EXPECT_NE(document.findFeature("Sphere002"),nullptr);
    EXPECT_EQ(data.features.size(),2);
}

TEST(DocumentDataTest, ImportedAssetStoresOriginalGeometrySeparatelyFromTranslation)
{
    ModelDocument document;
    const auto original = forge::geometry::ShapeFactory::translate(
        forge::geometry::ShapeFactory::makeBox(2,3,4),5,0,0);
    document.createImportedFeature(original,"source.step");
    document.setParameter("Imported001","x",100);
    const auto data = document.exportData();
    ASSERT_EQ(data.features.size(),1);
    const auto& feature = data.features[0];
    EXPECT_EQ(feature.sourceName,"source.step");
    EXPECT_DOUBLE_EQ(feature.parameters.at("x"),100);
    ASSERT_EQ(data.geometryAssets.size(),1);
    std::istringstream stream(data.geometryAssets.at(feature.geometryAsset));
    TopoDS_Shape restored;
    BRepTools::Read(restored,stream,BRep_Builder());
    ASSERT_FALSE(restored.IsNull());
    GProp_GProps props;
    BRepGProp::VolumeProperties(restored,props);
    EXPECT_NEAR(props.Mass(),24,1e-8);
    // 中心应是原始位置 5 + 半长 1；不能包含额外的 100 mm 平移。
    EXPECT_NEAR(props.CentreOfMass().X(),6,1e-8);
}
