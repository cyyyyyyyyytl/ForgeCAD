#include <gtest/gtest.h>
#include "application/ModelDocument.h"
#include "assistant/ToolRegistry.h"
#include "domain/AdvancedFeature.h"
#include "domain/BooleanFeature.h"
#include "geometry/AdvancedModeling.h"
#include "geometry/ShapeAnalyzer.h"
#include "infrastructure/DocumentJson.h"
#include "infrastructure/NativeDocumentIO.h"
#include <QTemporaryDir>
#include <BRepClass3d_SolidClassifier.hxx>
#include <numbers>
using forge::application::ModelDocument;
using forge::assistant::ToolRegistry;
using forge::domain::FeatureDefinition;
using forge::geometry::ShapeAnalyzer;
namespace {
auto measure(const ModelDocument& d,const std::string& id) {
    const auto r=d.rebuildReport().at(id);EXPECT_EQ(r.status,forge::core::RebuildStatus::Ready)<<r.message;
    return ShapeAnalyzer::analyze(r.shape);
}
QJsonObject run(ToolRegistry& t,const QString& name,const QJsonObject& a) {
    auto r=t.execute(name,a);EXPECT_TRUE(r["success"].toBool())<<name.toStdString()<<": "<<r["error"].toString().toStdString();return r;
}
}
TEST(AdvancedModelingTest,ConeFrustumCylinderAndTipVolumes) {
    for(const double top:{0.,10.,20.}) {
        ModelDocument d;d.createFeature("Cone",{{"radius_bottom",20},{"radius_top",top},{"height",30},{"x",5},{"z",7}});
        const auto a=measure(d,"Cone001");ASSERT_TRUE(a.volume);ASSERT_TRUE(a.bounds);EXPECT_EQ(a.solids,1);
        EXPECT_NEAR(*a.volume,std::numbers::pi*10*(400+20*top+top*top),1e-5);
        EXPECT_NEAR((*a.bounds)[0],-15,1e-6);EXPECT_NEAR((*a.bounds)[5],37,1e-6);
        d.setParameter("Cone001","radius_top",5);d.undo();EXPECT_NEAR(*measure(d,"Cone001").volume,*a.volume,1e-5);d.redo();
    }
    ModelDocument d;EXPECT_THROW(d.createFeature("Cone",{{"radius_bottom",0},{"radius_top",0},{"height",30}}),std::invalid_argument);EXPECT_FALSE(d.canUndo());
}
TEST(AdvancedModelingTest,SketchPlanesAndNormalExtrusionDirections) {
    for(int plane=0;plane<3;++plane)for(int dir=0;dir<3;++dir) {
        ModelDocument d;d.createFeature("RectangleSketch",{{"length",10},{"width",20},{"plane",double(plane)}});
        d.createExtrudeFeature("RectangleSketch001",6,static_cast<forge::domain::ExtrudeDirection>(dir));
        const auto a=measure(d,"Extrude001");ASSERT_TRUE(a.volume);ASSERT_TRUE(a.bounds);EXPECT_NEAR(*a.volume,1200,1e-5);
        const int axis=plane==0?2:plane==1?1:0;const double sign=plane==1?-1:1;
        const double lo=dir==2?-3:std::min(0.,(dir==1?-sign:sign)*6);const double hi=dir==2?3:std::max(0.,(dir==1?-sign:sign)*6);
        EXPECT_NEAR((*a.bounds)[axis],lo,1e-6);EXPECT_NEAR((*a.bounds)[axis+3],hi,1e-6);
    }
    ModelDocument d;EXPECT_THROW(d.createFeature("CircleSketch",{{"radius",2},{"plane",.5}}),std::invalid_argument);
}
TEST(AdvancedModelingTest,ClosedWallProfileRevolvesZAndEditsWithHistory) {
    ModelDocument d;FeatureDefinition wall;wall.vertices={{10,0,0},{15,0,0},{30,50,0},{25,50,0}};
    d.createAdvancedFeature("ProfileSketch",{{"plane",1}},wall);
    d.createRevolveFeature("ProfileSketch001",360,forge::domain::RevolveAxis::Z);
    auto a=measure(d,"Revolve001");ASSERT_TRUE(a.volume);EXPECT_EQ(a.solids,1);
    EXPECT_NEAR(*a.volume,std::numbers::pi*50/3*(225+450+900-100-250-625),1e-5);
    d.setParameter("Revolve001","angle",180);EXPECT_NEAR(*measure(d,"Revolve001").volume,*a.volume/2,1e-5);d.undo();
    wall.vertices[2].u=35;d.setAdvancedDefinition("ProfileSketch001",{{"plane",1}},wall);EXPECT_GT(*measure(d,"Revolve001").volume,*a.volume);
    d.undo();EXPECT_NEAR(*measure(d,"Revolve001").volume,*a.volume,1e-5);d.redo();
    EXPECT_EQ(d.visibleFeatureIds(),(std::vector<std::string>{"Revolve001"}));
    d.deleteFeature("ProfileSketch001");EXPECT_TRUE(d.features().empty());d.undo();EXPECT_EQ(d.features().size(),2u);
}
TEST(AdvancedModelingTest,ArcProfileAndInvalidDefinitionsAreAtomic) {
    ModelDocument d;FeatureDefinition profile;profile.vertices={{0,0,0},{20,0,1},{20,20,0},{0,20,0}};
    d.createAdvancedFeature("ProfileSketch",{},profile);d.createExtrudeFeature("ProfileSketch001",10);
    EXPECT_GT(*measure(d,"Extrude001").volume,4000);
    d.undo();EXPECT_TRUE(d.canRedo());const auto before=d.exportData();
    for(const auto& vertices:std::vector<std::vector<forge::domain::ProfileVertex>>{
        {{0,0,0},{10,10,0},{0,10,0},{10,0,0}},{{0,0,0},{0,0,0},{10,10,0}},{{0,0,0},{1,1,0}}}) {
        FeatureDefinition bad;bad.vertices=vertices;EXPECT_THROW(d.createAdvancedFeature("ProfileSketch",{},bad),std::invalid_argument);
        EXPECT_EQ(d.exportData().sequences,before.sequences);EXPECT_TRUE(d.canRedo());EXPECT_EQ(d.features().size(),1u);
    }
}
TEST(AdvancedModelingTest,TransformPreservesVolumeAndUsesWorldPivotThenTranslation) {
    ModelDocument d;d.createFeature("Box",{{"length",10},{"width",20},{"height",30},{"x",5}});
    d.createAdvancedFeature("Transform",{{"rz",90},{"pivot_x",5},{"x",100},{"y",10}}, {},{"Box001"});
    const auto a=measure(d,"Transform001");ASSERT_TRUE(a.bounds);EXPECT_NEAR(*a.volume,6000,1e-5);
    EXPECT_NEAR((*a.bounds)[0],85,1e-6);EXPECT_NEAR((*a.bounds)[3],105,1e-6);EXPECT_NEAR((*a.bounds)[1],10,1e-6);EXPECT_NEAR((*a.bounds)[4],20,1e-6);
    d.setParameter("Box001","length",15);EXPECT_NEAR(*measure(d,"Transform001").volume,9000,1e-5);
    d.setParameter("Transform001","rz",0);EXPECT_NEAR((*measure(d,"Transform001").bounds)[0],105,1e-6);d.undo();EXPECT_NEAR((*measure(d,"Transform001").bounds)[0],85,1e-6);
}
TEST(AdvancedModelingTest,FilletChamferRulesAndStaleExplicitEdges) {
    for(const auto type:{"Fillet","Chamfer"}) {
        ModelDocument d;d.createFeature("Box",{{"length",30},{"width",30},{"height",20}});
        d.createAdvancedFeature(type,{{type==std::string("Fillet")?"radius":"distance",2},{"selection",1}}, {},{"Box001"});
        const auto a=measure(d,std::string(type)+"001");EXPECT_EQ(a.solids,1);EXPECT_LT(*a.volume,18000);
        d.setParameter("Box001","height",25);EXPECT_EQ(measure(d,std::string(type)+"001").solids,1);
    }
    ModelDocument d;d.createFeature("Box",{{"length",30},{"width",30},{"height",20}});
    const auto edges=forge::geometry::AdvancedModeling::edges(d.rebuildReport().at("Box001").shape);ASSERT_EQ(edges.size(),12u);
    FeatureDefinition selected;selected.edgeIds={edges[0].id};d.createAdvancedFeature("Fillet",{{"radius",1},{"selection",4}},selected,{"Box001"});
    d.setParameter("Box001","height",25);EXPECT_EQ(d.rebuildReport().at("Fillet001").status,forge::core::RebuildStatus::Failed);
    EXPECT_EQ(d.visibleFeatureIds(),(std::vector<std::string>{"Box001"}));d.undo();EXPECT_EQ(measure(d,"Fillet001").solids,1);
    d.redo();ToolRegistry tools(d);
    const QString fresh=run(tools,"list_edges",{{"feature_id","Box001"}})["edges"].toArray()[0].toObject()["edge_id"].toString();
    run(tools,"set_edge_selection",{{"feature_id","Fillet001"},{"selection","explicit"},{"edge_ids",QJsonArray{fresh}}});
    EXPECT_EQ(measure(d,"Fillet001").solids,1);d.undo();EXPECT_EQ(d.rebuildReport().at("Fillet001").status,forge::core::RebuildStatus::Failed);
}
TEST(AdvancedModelingTest,ArcSweepMakesCurvedHandleAndAlignsProfile) {
    ModelDocument d;d.createFeature("CircleSketch",{{"radius",2},{"x",77},{"z",100}});
    FeatureDefinition path;path.pathPoints={{{10,0,0},std::array<double,3>{0,0,10}},{{-10,0,0},{}}};
    d.createAdvancedFeature("Path3D",{},path);d.createAdvancedFeature("Sweep",{}, {},{"CircleSketch001","Path3D001"});
    const auto a=measure(d,"Sweep001");EXPECT_EQ(a.solids,1);EXPECT_NEAR(*a.volume,40*std::numbers::pi*std::numbers::pi,1e-4);
    EXPECT_THROW(d.createAdvancedFeature("Sweep",{{"align_profile",0}}, {},{"CircleSketch001","Path3D001"}),std::invalid_argument);
    d.setParameter("CircleSketch001","radius",3);EXPECT_NEAR(*measure(d,"Sweep001").volume,90*std::numbers::pi*std::numbers::pi,1e-3);
}
TEST(AdvancedModelingTest,LoftHasOrderedSectionsAndUpdatesDependents) {
    ModelDocument d;d.createFeature("CircleSketch",{{"radius",10}});d.createFeature("CircleSketch",{{"radius",20},{"z",30}});
    d.createAdvancedFeature("Loft",{{"ruled",1}}, {},{"CircleSketch001","CircleSketch002"});
    const auto a=measure(d,"Loft001");EXPECT_EQ(a.solids,1);EXPECT_NEAR(*a.volume,7000*std::numbers::pi,1e-4);
    d.setParameter("CircleSketch002","z",60);EXPECT_NEAR(*measure(d,"Loft001").volume,*a.volume*2,1e-3);
    EXPECT_EQ(d.dependenciesOf("Loft001"),(std::vector<std::string>{"CircleSketch001","CircleSketch002"}));
    EXPECT_THROW(d.createAdvancedFeature("Loft",{}, {},{"CircleSketch001","CircleSketch001"}),std::invalid_argument);
}
TEST(AdvancedModelingTest,NativeRoundTripStructuredDefinitionsAndLegacyPlanes) {
    ModelDocument d;FeatureDefinition profile;profile.vertices={{10,0,0},{20,0,0},{20,30,0},{10,30,0}};
    d.createAdvancedFeature("ProfileSketch",{{"plane",1}},profile);d.createRevolveFeature("ProfileSketch001",360,forge::domain::RevolveAxis::Z);
    d.createAdvancedFeature("Fillet",{{"radius",1},{"selection",1}}, {},{"Revolve001"});
    FeatureDefinition path;path.pathPoints={{{0,0,0},std::array<double,3>{10,0,10}},{{0,0,20},{}}};d.createAdvancedFeature("Path3D",{},path);
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());const auto file=dir.filePath("advanced.forgecad");forge::infrastructure::NativeDocumentIO::save(d.exportData(),file);
    ModelDocument loaded;loaded.replaceData(forge::infrastructure::NativeDocumentIO::load(file));
    EXPECT_EQ(loaded.features().size(),4u);const auto* p=dynamic_cast<const forge::domain::AdvancedFeature*>(loaded.findFeature("Path3D001"));ASSERT_NE(p,nullptr);ASSERT_TRUE(p->definition().pathPoints[0].through);EXPECT_EQ((*p->definition().pathPoints[0].through)[0],10);
    EXPECT_NEAR(*measure(loaded,"Fillet001").volume,*measure(d,"Fillet001").volume,1e-5);
    loaded.setParameter("ProfileSketch001","z",5);loaded.undo();EXPECT_NEAR(*measure(loaded,"Fillet001").volume,*measure(d,"Fillet001").volume,1e-5);
    ModelDocument old;old.createFeature("CircleSketch",{{"radius",2}});auto data=old.exportData();data.features[0].parameters.erase("plane");data.features[0].parameters.erase("z");loaded.replaceData(data);EXPECT_EQ(loaded.findFeature("CircleSketch001")->parameters().back().asDouble(),0);
}
TEST(AdvancedToolsTest,FullToolChainAndStrictNestedArguments) {
    ModelDocument d;ToolRegistry tools(d);
    QJsonArray vertices{QJsonObject{{"u",10},{"v",0}},QJsonObject{{"u",20},{"v",0}},QJsonObject{{"u",20},{"v",30}},QJsonObject{{"u",10},{"v",30}}};
    run(tools,"create_profile",{{"vertices",vertices},{"plane","XZ"}});run(tools,"create_revolve",{{"sketch_id","ProfileSketch001"},{"angle",360},{"axis","Z"}});
    run(tools,"create_fillet",{{"base_id","Revolve001"},{"radius",1}});run(tools,"create_chamfer",{{"base_id","Fillet001"},{"distance",.5},{"selection","bottom"}});
    run(tools,"list_edges",{{"feature_id","Chamfer001"}});run(tools,"create_transform",{{"source_id","Chamfer001"},{"rx",90},{"x",50}});
    run(tools,"create_path",{{"points",QJsonArray{QJsonObject{{"x",0},{"y",0},{"z",0}},QJsonObject{{"x",0},{"y",0},{"z",30}}}}});
    run(tools,"create_feature",{{"type","CircleSketch"},{"parameters",QJsonObject{{"radius",2}}}});run(tools,"create_sweep",{{"profile_id","CircleSketch001"},{"path_id","Path3D001"}});
    run(tools,"set_path",{{"feature_id","Path3D001"},{"points",QJsonArray{QJsonObject{{"x",0},{"y",0},{"z",0}},QJsonObject{{"x",0},{"y",0},{"z",50}}}}});
    EXPECT_NEAR(*measure(d,"Sweep001").volume,200*std::numbers::pi,1e-4);
    auto feature=run(tools,"get_feature",{{"feature_id","ProfileSketch001"}});EXPECT_EQ(feature["definition"].toObject()["vertices"].toArray().size(),4);
    run(tools,"set_profile",{{"feature_id","ProfileSketch001"},{"vertices",vertices},{"z",5}});
    run(tools,"create_feature",{{"type","CircleSketch"},{"parameters",QJsonObject{{"radius",8},{"z",60}}}});
    run(tools,"create_loft",{{"profile_ids",QJsonArray{"CircleSketch001","CircleSketch002"}},{"ruled",true}});
    run(tools,"create_feature",{{"type","Cone"},{"parameters",QJsonObject{{"radius_bottom",10},{"radius_top",20},{"height",30}}}});
    const auto data=forge::infrastructure::DocumentJson::decode(forge::infrastructure::DocumentJson::encode(d.exportData()));
    ModelDocument restored;restored.replaceData(data);
    for (const auto* feature:{"Transform001","Sweep001","Loft001","Cone001"}) EXPECT_NEAR(*measure(restored,feature).volume,*measure(d,feature).volume,1e-4);
    const auto count=d.features().size();
    for(const auto& bad:std::vector<QJsonObject>{{{"vertices",vertices},{"closed",1}},{{"vertices",QJsonArray{QJsonObject{{"u",0},{"v",0},{"unknown",2}},QJsonObject{{"u",1},{"v",1}}}}},{{"vertices",vertices},{"plane","ZX"}}}) EXPECT_FALSE(tools.execute("create_profile",bad)["success"].toBool());
    EXPECT_FALSE(tools.execute("create_path",{{"points",QJsonArray{QJsonObject{{"x",0},{"y",0},{"z",0},{"through",QJsonObject{{"x",1}}}},QJsonObject{{"x",0},{"y",0},{"z",1}}}}})["success"].toBool());
    EXPECT_FALSE(tools.execute("create_fillet",{{"base_id","Revolve001"},{"radius",1},{"selection","explicit"}})["success"].toBool());
    EXPECT_EQ(d.features().size(),count);
}

TEST(AdvancedModelingTest,HollowCurvedCupLoftStemAndSweptHandlesFormSingleTrophy) {
    ModelDocument d;
    d.createFeature("Cone",{{"radius_bottom",35},{"radius_top",28},{"height",12}});
    d.createAdvancedFeature("Fillet",{{"radius",1.5},{"selection",1}}, {},{"Cone001"});
    d.createFeature("CircleSketch",{{"radius",9},{"z",10}});
    d.createFeature("CircleSketch",{{"radius",7},{"z",103}});
    d.createAdvancedFeature("Loft",{{"ruled",1}}, {},{"CircleSketch001","CircleSketch002"});
    FeatureDefinition cup;cup.vertices={{0,100,0},{16,100,.1},{40,160,0},{36,160,-.1},{13,105,0},{0,105,0}};
    d.createAdvancedFeature("ProfileSketch",{{"plane",1}},cup);d.createRevolveFeature("ProfileSketch001",360,forge::domain::RevolveAxis::Z);
    FeatureDefinition path;path.pathPoints={{{22,0,110},std::array<double,3>{55,0,130}},{{38,0,150},{}}};
    d.createAdvancedFeature("Path3D",{},path);d.createFeature("CircleSketch",{{"radius",2.5}});
    d.createAdvancedFeature("Sweep",{}, {},{"CircleSketch003","Path3D001"});
    d.createAdvancedFeature("Transform",{{"rz",180}}, {},{"Sweep001"});
    const auto unite=[&](const std::string& a,const std::string& b){return d.createBooleanFeature(forge::domain::BooleanOperation::Union,a,b).id();};
    auto id=unite("Fillet001","Loft001");id=unite(id,"Revolve001");id=unite(id,"Sweep001");id=unite(id,"Transform001");
    const auto a=measure(d,id);EXPECT_EQ(a.solids,1);EXPECT_GT(*a.volume,0);
    const auto shape=d.rebuildReport().at(id).shape;
    BRepClass3d_SolidClassifier cavity(shape,gp_Pnt(0,0,140),1e-6);EXPECT_EQ(cavity.State(),TopAbs_OUT);
    BRepClass3d_SolidClassifier bottom(shape,gp_Pnt(0,0,102),1e-6);EXPECT_EQ(bottom.State(),TopAbs_IN);
}
