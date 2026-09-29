#include <gtest/gtest.h> // GoogleTest 的测试定义和断言宏。

#include "application/ModelDocument.h" // 工具最终读写的真实文档。
#include "assistant/ToolRegistry.h"     // 被测的 JSON 安全边界。
#include "domain/Feature.h"             // 读取工具修改后的领域参数。
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <GProp_GProps.hxx>
#include <numbers>
#include <set>
#include <limits>

using forge::application::ModelDocument;
using forge::assistant::ToolRegistry;

// ============================================================
// ToolRegistryTest：验证 LLM JSON 到 ModelDocument 的安全边界
// ------------------------------------------------------------
// 测试完全不访问 DeepSeek 网络；直接构造模型可能返回的 QJsonObject，
// 因此结果稳定、无费用，也能精确定位是协议层还是网络层出错。
// ============================================================

// 登记的名称必须唯一，且覆盖当前全部建模、文件和窗口能力。
TEST(ToolRegistryTest, ExposesAllCurrentOperations)
{
    ModelDocument document;         // 使用空文档隔离本测试。
    ToolRegistry registry(document); // Registry 保存对该文档的非拥有引用。

    // 同时抽查首尾工具名，避免数量正确但删除工具未正确登记。
    const QJsonArray schemas = registry.schemas();
    ASSERT_EQ(schemas.size(), 37);
    std::set<std::string> names;
    for (const auto& entry : schemas) {
        const auto function=entry.toObject().value("function").toObject();
        EXPECT_TRUE(names.insert(function.value("name").toString().toStdString()).second);
        EXPECT_FALSE(function.value("description").toString().isEmpty());
        EXPECT_FALSE(function.value("parameters").toObject().value("additionalProperties").toBool());
    }
    EXPECT_EQ(names,(std::set<std::string>{"list_features","get_feature","create_feature","set_parameter","delete_feature",
        "get_feature_types","get_document_status","set_position","create_extrude","create_extrude_cut","create_revolve","create_boolean",
        "undo","redo","new_document","open_document","save_document","save_document_as","import_step","export_step",
        "select_feature","begin_point_placement","cancel_point_placement","control_view","get_spatial_rules","analyze_geometry","create_profile","set_profile","create_path","set_path","create_transform","list_edges","set_edge_selection","create_fillet","create_chamfer","create_sweep","create_loft"}));
    EXPECT_EQ(schemas[0].toObject()["function"].toObject()["name"].toString(),
              "list_features");
    EXPECT_EQ(schemas[4].toObject()["function"].toObject()["name"].toString(),
              "delete_feature");
}

namespace {
double toolVolume(ModelDocument& document,const std::string& id)
{
    const auto result=document.rebuildReport().at(id);
    EXPECT_EQ(result.status,forge::core::RebuildStatus::Ready);
    if (result.shape.IsNull()) return 0;
    EXPECT_TRUE(BRepCheck_Analyzer(result.shape).IsValid());
    GProp_GProps properties; BRepGProp::VolumeProperties(result.shape,properties); return properties.Mass();
}
}

TEST(ToolRegistryTest, CreatesRingEditsHalfRingAndReportsUpstreamFailureWithHistory)
{
    ModelDocument document; ToolRegistry registry(document);
    ASSERT_TRUE(registry.execute("create_feature",{{"type","CircleSketch"},{"parameters",QJsonObject{{"radius",2},{"x",10}}}}).value("success").toBool());
    auto result=registry.execute("create_revolve",{{"sketch_id","CircleSketch001"},{"angle",360},{"axis","Y"}});
    ASSERT_TRUE(result.value("success").toBool());
    EXPECT_EQ(result.value("dependencies").toArray(),(QJsonArray{"CircleSketch001"}));
    EXPECT_NEAR(toolVolume(document,"Revolve001"),80*std::numbers::pi*std::numbers::pi,1e-6);
    ASSERT_TRUE(registry.execute("set_parameter",{{"feature_id","Revolve001"},{"parameter_name","angle"},{"value",180}}).value("success").toBool());
    EXPECT_NEAR(toolVolume(document,"Revolve001"),40*std::numbers::pi*std::numbers::pi,1e-6);
    ASSERT_TRUE(registry.execute("undo",{}).value("success").toBool());
    EXPECT_NEAR(toolVolume(document,"Revolve001"),80*std::numbers::pi*std::numbers::pi,1e-6);
    ASSERT_TRUE(registry.execute("redo",{}).value("success").toBool());
    EXPECT_NEAR(toolVolume(document,"Revolve001"),40*std::numbers::pi*std::numbers::pi,1e-6);
    result=registry.execute("set_position",{{"feature_id","CircleSketch001"},{"x",0},{"y",0},{"z",0}});
    EXPECT_TRUE(result.value("success").toBool());
    const auto failures=result.value("document_status").toObject().value("failed_features").toArray();
    ASSERT_EQ(failures.size(),1);
    EXPECT_TRUE(failures[0].toObject().value("rebuild_message").toString().contains(QStringLiteral("跨越")));
    EXPECT_EQ(document.visibleFeatureIds(),(std::vector<std::string>{"CircleSketch001"}));
    ASSERT_TRUE(registry.execute("undo",{}).value("success").toBool());
    EXPECT_NEAR(toolVolume(document,"Revolve001"),40*std::numbers::pi*std::numbers::pi,1e-6);
}

TEST(ToolRegistryTest, RejectsMalformedRevolveAndPreservesRedoAndNumbering)
{
    ModelDocument document; ToolRegistry registry(document);
    document.createFeature("CircleSketch",{{"radius",2},{"x",10}});
    document.createRevolveFeature("CircleSketch001",360); document.undo();
    for (const auto& arguments : {
        QJsonObject{{"sketch_id","CircleSketch001"},{"angle",0}},
        QJsonObject{{"sketch_id","CircleSketch001"},{"angle",360},{"axis","Z"}},
        QJsonObject{{"sketch_id","CircleSketch001"},{"angle","180"}},
        QJsonObject{{"sketch_id","CircleSketch001"},{"angle",180},{"extra",1}},
        QJsonObject{{"sketch_id","CircleSketch001"},{"angle",360},{"axis","X"}},
        QJsonObject{{"sketch_id","missing"},{"angle",360}}}) {
        EXPECT_FALSE(registry.execute("create_revolve",arguments).value("success").toBool());
        EXPECT_TRUE(document.canRedo()); EXPECT_EQ(document.features().size(),1);
    }
    ASSERT_TRUE(registry.execute("redo",{}).value("success").toBool());
    EXPECT_NEAR(toolVolume(document,"Revolve001"),80*std::numbers::pi*std::numbers::pi,1e-6);
    const auto created=registry.execute("create_revolve",{{"sketch_id","CircleSketch001"},{"angle",180}});
    EXPECT_EQ(created.value("feature_id").toString(),"Revolve002");
    ModelDocument xDocument; ToolRegistry xRegistry(xDocument);
    xDocument.createFeature("CircleSketch",{{"radius",2},{"y",10}});
    ASSERT_TRUE(xRegistry.execute("create_revolve",{{"sketch_id","CircleSketch001"},{"angle",180},{"axis","X"}}).value("success").toBool());
    EXPECT_NEAR(toolVolume(xDocument,"Revolve001"),40*std::numbers::pi*std::numbers::pi,1e-6);
}

TEST(ToolRegistryTest, CreatesBothSketchesAndAllExtrudeDirections)
{
    for (const char* direction : {"forward","reverse","symmetric"}) {
        ModelDocument document; ToolRegistry registry(document);
        ASSERT_TRUE(registry.execute("create_feature",{{"type","RectangleSketch"},{"parameters",QJsonObject{{"length",10},{"width",20}}}}).value("success").toBool());
        auto result=registry.execute("create_extrude",{{"sketch_id","RectangleSketch001"},{"height",5},{"direction",direction}});
        ASSERT_TRUE(result.value("success").toBool());
        EXPECT_NEAR(toolVolume(document,"Extrude001"),1000,1e-6);
        EXPECT_EQ(result.value("dependencies").toArray(),(QJsonArray{"RectangleSketch001"}));
    }
    ModelDocument document; ToolRegistry registry(document);
    document.createFeature("CircleSketch",{{"radius",2}});
    EXPECT_TRUE(registry.execute("create_extrude",{{"sketch_id","CircleSketch001"},{"height",5}}).value("success").toBool());
    EXPECT_NEAR(toolVolume(document,"Extrude001"),20*std::numbers::pi,1e-6);
}

TEST(ToolRegistryTest, CreatesCutAndThreeBooleanOperationsWithOrderedInputs)
{
    for (const char* operation : {"difference","union","intersection"}) {
        ModelDocument document; ToolRegistry registry(document);
        document.createFeature("Box",{{"length",10},{"width",10},{"height",10}});
        document.createFeature("Box",{{"length",5},{"width",5},{"height",5}});
        const auto result=registry.execute("create_boolean",{{"operation",operation},{"base_id","Box001"},{"tool_id","Box002"}});
        ASSERT_TRUE(result.value("success").toBool());
        EXPECT_EQ(result.value("dependencies").toArray(),(QJsonArray{"Box001","Box002"}));
        const double expected=std::string(operation)=="difference" ? 875 : std::string(operation)=="union" ? 1000 : 125;
        EXPECT_NEAR(toolVolume(document,result.value("feature_id").toString().toStdString()),expected,1e-6);
        EXPECT_FALSE(registry.execute("create_boolean",{{"operation",operation},{"base_id","Box001"},{"tool_id","Box001"}}).value("success").toBool());
    }
    ModelDocument document; ToolRegistry registry(document);
    document.createFeature("Box",{{"length",10},{"width",10},{"height",10}});
    document.createFeature("CircleSketch",{{"radius",2},{"x",5},{"y",5}});
    const auto cut=registry.execute("create_extrude_cut",{{"base_id","Box001"},{"sketch_id","CircleSketch001"},{"height",10}});
    ASSERT_TRUE(cut.value("success").toBool());
    EXPECT_EQ(cut.value("dependencies").toArray(),(QJsonArray{"Box001","CircleSketch001"}));
    EXPECT_NEAR(toolVolume(document,"ExtrudeCut001"),1000-40*std::numbers::pi,1e-6);
}

TEST(ToolRegistryTest, PositionIsAtomicAndInvalidArgumentsNeverMutate)
{
    ModelDocument document; ToolRegistry registry(document);
    EXPECT_FALSE(registry.execute("undo",{}).value("success").toBool());
    document.createFeature("Box",{{"length",10},{"width",10},{"height",10}});
    ASSERT_TRUE(registry.execute("set_position",{{"feature_id","Box001"},{"x",10},{"y",20},{"z",30}}).value("success").toBool());
    ASSERT_TRUE(registry.execute("undo",{}).value("success").toBool());
    const auto parameters=registry.execute("get_feature",{{"feature_id","Box001"}}).value("parameters").toObject();
    for (const char* axis : {"x","y","z"}) EXPECT_DOUBLE_EQ(parameters.value(axis).toDouble(),0);
    EXPECT_FALSE(registry.execute("set_position",{{"feature_id","Box001"},{"x",1},{"y",2}}).value("success").toBool());
    EXPECT_FALSE(registry.execute("set_parameter",{{"feature_id","Box001"},{"parameter_name","x"},{"value",std::numeric_limits<double>::infinity()}}).value("success").toBool());
    EXPECT_FALSE(registry.execute("set_parameter",{{"feature_id","Box001"},{"parameter_name","x"},{"value",1},{"typo",2}}).value("success").toBool());
    EXPECT_TRUE(document.canRedo());
    EXPECT_TRUE(registry.execute("redo",{}).value("success").toBool());
    EXPECT_FALSE(registry.execute("redo",{}).value("success").toBool());
}

TEST(ToolRegistryTest, DiscoversRulesAndRejectsUnavailableUiTools)
{
    ModelDocument document; ToolRegistry registry(document);
    const auto types=registry.execute("get_feature_types",{}).value("types").toArray();
    EXPECT_EQ(types.size(),20);
    bool found=false;
    for (const auto& value : types) if (value.toObject().value("type").toString()=="Revolve") {
        const auto type=value.toObject(); found=true;
        EXPECT_EQ(type.value("creation_tool").toString(),"create_revolve");
        EXPECT_EQ(type.value("parameters").toArray()[0].toObject().value("unit").toString(),"degree");
    }
    EXPECT_TRUE(found);
    EXPECT_FALSE(registry.execute("new_document",{}).value("success").toBool());
    EXPECT_FALSE(registry.execute("control_view",{{"operation","zoom"}}).value("success").toBool());
    EXPECT_FALSE(registry.execute("control_view",{{"operation","fit"},{"factor",2}}).value("success").toBool());
    EXPECT_FALSE(registry.execute("save_document_as",{{"path",42}}).value("success").toBool());
    EXPECT_EQ(registry.execute("get_document_status",{}).value("feature_count").toInt(),0);
}

// 模拟模型请求 create_feature，再用 list_features 查询刚创建的对象。
TEST(ToolRegistryTest, CreatesAndListsFeatureFromJsonArguments)
{
    ModelDocument document;          // 创建工具所操作的实际文档。
    ToolRegistry registry(document); // 把文档注入 AI 工具边界。

    // JSON 结构与 DeepSeek function.arguments 解析后的对象一致。
    const QJsonObject created = registry.execute("create_feature", {
        {"type", "Box"},
        {"parameters", QJsonObject{
            {"length", 100.0},
            {"width", 50.0},
            {"height", 30.0},
        }},
    });

    // 除 success 外，修改工具必须标记 model_changed，UI 才会刷新三维视图。
    EXPECT_TRUE(created["success"].toBool());              // 本地创建执行成功。
    EXPECT_TRUE(created["model_changed"].toBool());        // Controller 应刷新 UI。
    EXPECT_EQ(created["feature_id"].toString(), "Box001"); // 返回稳定 ID 给模型。
    ASSERT_NE(document.findFeature("Box001"), nullptr);     // 文档中确实存在对象。

    // 查询工具不修改 Document，但应返回包含 Box001 的数组。
    const QJsonObject listed = registry.execute("list_features", {});
    EXPECT_TRUE(listed["success"].toBool());          // 查询本身成功。
    ASSERT_EQ(listed["features"].toArray().size(), 1); // 返回刚创建的唯一对象。
}

// 验证成功修改、越界拒绝和未知工具拒绝三条分支。
TEST(ToolRegistryTest, ModifiesFeatureAndRejectsInvalidCalls)
{
    ModelDocument document;          // 为修改工具准备独立文档。
    ToolRegistry registry(document); // 工具持有文档引用。
    document.createFeature("Sphere", {{"radius", 20.0}}); // 建立待修改对象。

    // 合法请求把 Sphere001.radius 从 20 修改为 35。
    const QJsonObject modified = registry.execute("set_parameter", {
        {"feature_id", "Sphere001"},
        {"parameter_name", "radius"},
        {"value", 35.0},
    });
    EXPECT_TRUE(modified["success"].toBool()); // Registry 已执行 Document 修改。
    EXPECT_DOUBLE_EQ(document.findFeature("Sphere001")->parameters()[0].asDouble(), 35.0);

    // 非法负半径应返回 success=false，而不是让异常逃出 Registry。
    EXPECT_FALSE(registry.execute("set_parameter", {
        {"feature_id", "Sphere001"},
        {"parameter_name", "radius"},
        {"value", -1.0},
    })["success"].toBool());
    // 工具白名单外的名称必须拒绝，防止模型调用未授权能力。
    EXPECT_FALSE(registry.execute("unknown_tool", {})["success"].toBool());
}

// 删除工具要报告已删除的 ID，并允许用户随后通过 Document::undo() 恢复。
TEST(ToolRegistryTest, DeletesFeatureAndCanUndo)
{
    ModelDocument document;
    ToolRegistry registry(document);
    document.createFeature("Sphere", {{"radius", 20.0}});

    const QJsonObject deleted = registry.execute("delete_feature", {{"feature_id", "Sphere001"}});
    EXPECT_TRUE(deleted["success"].toBool());
    EXPECT_TRUE(deleted["model_changed"].toBool());
    EXPECT_EQ(deleted["feature_id"].toString(), "Sphere001");
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr);

    document.undo();
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);
}

// 工具回执列出级联删除的全部 ID，供 AI 如实告知用户实际删除范围。
TEST(ToolRegistryTest, ReportsAllCascadeDeletedFeatures)
{
    ModelDocument document;
    ToolRegistry registry(document);
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 20.0}, {"height", 30.0},
    });
    document.createFeature("Sphere", {{"radius", 20.0}});
    document.addDependency("Sphere001", "Box001");

    const QJsonObject deleted = registry.execute("delete_feature", {{"feature_id", "Box001"}});
    EXPECT_TRUE(deleted["success"].toBool());
    const QJsonArray ids = deleted["deleted_feature_ids"].toArray();
    ASSERT_EQ(ids.size(), 2);
    EXPECT_EQ(ids[0].toString(), "Sphere001");
    EXPECT_EQ(ids[1].toString(), "Box001");
    EXPECT_TRUE(document.features().empty());

    document.undo();
    EXPECT_NE(document.findFeature("Box001"), nullptr);
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);
}

// 非法目标不应删除已有特征，也不应新增一次撤销记录。
TEST(ToolRegistryTest, RejectsInvalidDeleteWithoutChangingDocument)
{
    ModelDocument document;
    ToolRegistry registry(document);
    document.createFeature("Sphere", {{"radius", 20.0}});

    EXPECT_FALSE(registry.execute("delete_feature", {})["success"].toBool());
    EXPECT_FALSE(registry.execute("delete_feature", {{"feature_id", "Sphere999"}})["success"].toBool());
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);

    // 撤销栈应仍只含最初的创建：一次 undo 后文档应为空。
    document.undo();
    EXPECT_TRUE(document.features().empty());
    EXPECT_FALSE(document.canUndo());
}
