#include <gtest/gtest.h> // GoogleTest 的 TEST、EXPECT_* 和 ASSERT_* 宏。

#include "application/ModelDocument.h" // 本文件直接验证的文档 API。
#include "domain/BooleanFeature.h"     // 布尔创建测试使用运算枚举。
#include "domain/Feature.h"             // 断言需要读取查找到的 Feature 参数。
#include "geometry/ShapeFactory.h"      // 畸形导入数据测试需要构造一个合法 B-Rep。

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

using forge::application::ModelDocument; // 缩短测试代码中的完整命名空间前缀。

namespace {

// 测试辅助函数：按稳定 ID 和参数下标读取数值，减少 Undo/Redo 用例中的重复代码。
double parameterValue(const ModelDocument& document,
                      std::string_view featureId,
                      std::size_t index)
{
    const auto* feature = document.findFeature(featureId); // const 文档调用只读 findFeature 重载。
    EXPECT_NE(feature, nullptr); // 先记录“对象必须存在”的测试失败信息。
    // EXPECT 失败不会停止测试，所以用条件表达式避免随后解引用空指针导致崩溃。
    return feature ? feature->parameters().at(index).asDouble() : 0.0;
}

double shapeVolume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

} // namespace

TEST(ModelDocumentTest, RebuildShapesTracksDocumentChanges)
{
    ModelDocument document;
    EXPECT_TRUE(document.rebuildShapes().empty());

    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    document.createFeature("Cylinder", {
        {"radius", 2.0}, {"height", 10.0},
    });
    document.addDependency("Cylinder001", "Box001");

    const auto initial = document.rebuildShapes();
    ASSERT_EQ(initial.size(), 2u);
    ASSERT_FALSE(initial.at("Box001").IsNull());
    ASSERT_FALSE(initial.at("Cylinder001").IsNull());

    GProp_GProps originalProperties;
    BRepGProp::VolumeProperties(initial.at("Box001"), originalProperties);
    EXPECT_NEAR(originalProperties.Mass(), 1000.0, 1e-5);

    document.setParameter("Box001", "length", 12.0);
    const auto changed = document.rebuildShapes();
    GProp_GProps changedProperties;
    BRepGProp::VolumeProperties(changed.at("Box001"), changedProperties);
    EXPECT_NEAR(changedProperties.Mass(), 1200.0, 1e-5);

    document.undo();
    const auto restored = document.rebuildShapes();
    GProp_GProps restoredProperties;
    BRepGProp::VolumeProperties(restored.at("Box001"), restoredProperties);
    EXPECT_NEAR(restoredProperties.Mass(), 1000.0, 1e-5);

    document.deleteFeature("Box001");
    EXPECT_TRUE(document.rebuildShapes().empty());
}

TEST(ModelDocumentTest, BooleanFeatureUsesInputsAndSurvivesHistory)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    document.createFeature("Box", {
        {"length", 5.0}, {"width", 5.0}, {"height", 5.0},
    });

    auto& cut = document.createBooleanFeature(
        forge::domain::BooleanOperation::Difference, "Box001", "Box002");
    EXPECT_EQ(cut.id(), "Cut001");
    EXPECT_EQ(cut.type(), "Cut");
    const auto initial = document.rebuildShapes();
    ASSERT_FALSE(initial.at("Cut001").IsNull());
    EXPECT_NEAR(shapeVolume(initial.at("Cut001")), 875.0, 1e-5);

    document.setParameter("Box002", "length", 4.0);
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Cut001")), 900.0, 1e-5);

    document.undo(); // 撤销工具形状的尺寸修改。
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Cut001")), 875.0, 1e-5);
    document.undo(); // 一次撤销就移除布尔特征和两条依赖。
    EXPECT_EQ(document.findFeature("Cut001"), nullptr);
    document.redo();
    ASSERT_NE(document.findFeature("Cut001"), nullptr);
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Cut001")), 875.0, 1e-5);

    document.deleteFeature("Box001");
    EXPECT_EQ(document.findFeature("Cut001"), nullptr);
    EXPECT_NE(document.findFeature("Box002"), nullptr);
}

TEST(ModelDocumentTest, BooleanCreationRejectsInvalidInputsWithoutHistory)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });

    EXPECT_THROW(document.createBooleanFeature(
        forge::domain::BooleanOperation::Difference, "Box001", "Box001"),
        std::invalid_argument);
    EXPECT_THROW(document.createBooleanFeature(
        forge::domain::BooleanOperation::Union, "Box001", "missing"),
        std::invalid_argument);
    EXPECT_EQ(document.features().size(), 1u);
    document.undo(); // 失败创建不增加历史，所以撤销的是最初的 Box。
    EXPECT_TRUE(document.features().empty());
}

TEST(ModelDocumentTest, UnionAndIntersectionRestoreWithTheirDependencies)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    document.createFeature("Box", {
        {"length", 5.0}, {"width", 5.0}, {"height", 5.0},
    });

    document.createBooleanFeature(
        forge::domain::BooleanOperation::Union, "Box001", "Box002");
    document.createBooleanFeature(
        forge::domain::BooleanOperation::Intersection, "Box001", "Box002");
    EXPECT_THROW(document.addDependency("Union001", "Intersection001"),
                 std::invalid_argument);

    document.undo(); // 撤销交集创建。
    EXPECT_EQ(document.findFeature("Intersection001"), nullptr);
    document.redo(); // 重新从快照恢复交集类型及其两条依赖。

    const auto shapes = document.rebuildShapes();
    ASSERT_FALSE(shapes.at("Union001").IsNull());
    ASSERT_FALSE(shapes.at("Intersection001").IsNull());
    EXPECT_NEAR(shapeVolume(shapes.at("Union001")), 1000.0, 1e-5);
    EXPECT_NEAR(shapeVolume(shapes.at("Intersection001")), 125.0, 1e-5);
}

TEST(ModelDocumentTest, VisibilityFollowsBooleanChainAndUndo)
{
    ModelDocument document;
    EXPECT_TRUE(document.visibleFeatureIds().empty());
    for (int i = 0; i < 3; ++i) {
        document.createFeature("Box", {
            {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
        });
    }
    document.createFeature("Sphere", {{"radius", 2.0}}); // 无关形状保持可见。
    document.createBooleanFeature(
        forge::domain::BooleanOperation::Union, "Box001", "Box002");
    EXPECT_EQ(document.visibleFeatureIds(),
              (std::vector<std::string>{"Box003", "Sphere001", "Union001"}));

    document.createBooleanFeature(
        forge::domain::BooleanOperation::Intersection, "Union001", "Box003");
    EXPECT_EQ(document.visibleFeatureIds(),
              (std::vector<std::string>{"Sphere001", "Intersection001"}));
    EXPECT_EQ(document.features().size(), 6u); // 隐藏只影响显示，不删除模型。

    document.undo();
    EXPECT_EQ(document.visibleFeatureIds(),
              (std::vector<std::string>{"Box003", "Sphere001", "Union001"}));
    document.redo();
    EXPECT_EQ(document.visibleFeatureIds(),
              (std::vector<std::string>{"Sphere001", "Intersection001"}));
    document.deleteFeature("Intersection001");
    EXPECT_EQ(document.visibleFeatureIds(),
              (std::vector<std::string>{"Box003", "Sphere001", "Union001"}));
}

TEST(ModelDocumentTest, OrdinaryDependenciesDoNotHideShapes)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    document.createFeature("Sphere", {{"radius", 2.0}});
    document.addDependency("Sphere001", "Box001");
    EXPECT_EQ(document.visibleFeatureIds(),
              (std::vector<std::string>{"Box001", "Sphere001"}));
}

// 验证不同类型各自从 001 编号，同类型继续递增，且文档支持按 ID 查找。
TEST(ModelDocumentTest, CreatesAndFindsFeaturesWithStableIds)
{
    ModelDocument document; // 每个测试使用独立空文档，互不共享历史或序号。
    // 创建第一个 Box，保存返回引用以检查文档分配的稳定 ID。
    auto& firstBox = document.createFeature("Box", {
        {"length", 100.0}, {"width", 50.0}, {"height", 30.0},
    });
    // Sphere 使用独立类型计数，因此应得到 Sphere001 而不是 Sphere002。
    auto& sphere = document.createFeature("Sphere", {{"radius", 20.0}});
    // 再建 Box 应沿用 Box 自己的计数器得到 Box002。
    auto& secondBox = document.createFeature("Box", {
        {"length", 20.0}, {"width", 20.0}, {"height", 20.0},
    });

    EXPECT_EQ(firstBox.id(), "Box001");             // 第一个 Box 的编号从 001 开始。
    EXPECT_EQ(sphere.id(), "Sphere001");            // Sphere 拥有自己的序号空间。
    EXPECT_EQ(secondBox.id(), "Box002");            // 第二个 Box 连续递增。
    EXPECT_EQ(document.features().size(), 3u);       // 三次成功创建都进入文档。
    EXPECT_EQ(document.findFeature("missing"), nullptr); // 未知 ID 按约定返回空指针。
}

// 验证历史是后进先出：先撤销参数修改，再撤销更早的创建。
TEST(ModelDocumentTest, CreateAndParameterChangeUndoInLifoOrder)
{
    ModelDocument document; // 初始状态为空文档。
    document.createFeature("Sphere", {{"radius", 20.0}}); // 历史 1：创建球体。
    document.setParameter("Sphere001", "radius", 35.0);   // 历史 2：修改半径。

    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 35.0); // 修改已生效。

    document.undo(); // 撤销最近的参数修改，球体仍然存在。
    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 20.0); // 恢复旧半径。

    document.undo(); // 再撤销创建，回到最初的空文档。
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr); // 球体已不存在。

    document.redo(); // 第一次重做恢复创建时的半径 20。
    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 20.0);
    document.redo(); // 第二次重做恢复随后发生的修改。
    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 35.0);
}

// 验证删除的 Undo 不仅恢复对象，还恢复它在 vector 中的原始顺序。
TEST(ModelDocumentTest, DeleteUndoRestoresFeatureAndOrder)
{
    ModelDocument document; // 依次建立 Box、Sphere、Cylinder 三项文档。
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 20.0}, {"height", 30.0},
    });
    document.createFeature("Sphere", {{"radius", 20.0}});
    document.createFeature("Cylinder", {{"radius", 15.0}, {"height", 40.0}});

    document.deleteFeature("Sphere001");               // 删除中间的第二项。
    ASSERT_EQ(document.features().size(), 2u);           // ASSERT 保证后续下标访问安全。
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr); // 被删 ID 应无法查到。

    document.undo(); // 恢复删除前的整份 DocumentState。
    ASSERT_EQ(document.features().size(), 3u);
    EXPECT_EQ(document.features()[0]->id(), "Box001");
    EXPECT_EQ(document.features()[1]->id(), "Sphere001");
    EXPECT_EQ(document.features()[2]->id(), "Cylinder001");

    document.redo(); // 再次应用删除后的状态。
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr);
}

// 间接验证快照也保存了依赖图：重做创建后，删除仍能从图里找到该节点。
TEST(ModelDocumentTest, CreateUndoRedoRestoresDependencyNode)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 20.0}, {"height", 30.0},
    });

    document.undo();
    EXPECT_EQ(document.findFeature("Box001"), nullptr);

    document.redo();
    ASSERT_NE(document.findFeature("Box001"), nullptr);

    document.deleteFeature("Box001");
    EXPECT_EQ(document.findFeature("Box001"), nullptr);

    document.undo();
    EXPECT_NE(document.findFeature("Box001"), nullptr);
}

// 删除预览只返回名单；它既不删 Feature，也不新增一条 Undo 历史。
TEST(ModelDocumentTest, DeletionPreviewDoesNotChangeDocument)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 20.0}, {"height", 30.0},
    });

    EXPECT_EQ(document.deletionOrder("Box001"),
              (std::vector<std::string>{"Box001"}));
    EXPECT_NE(document.findFeature("Box001"), nullptr);
    EXPECT_THROW(document.deletionOrder("missing"), std::invalid_argument);

    document.undo(); // 唯一历史仍是创建 Box；预览没有产生新历史。
    EXPECT_TRUE(document.features().empty());
    EXPECT_FALSE(document.canUndo());
}

// 一条上游依赖删除其全部下游；整个级联操作用一次 Undo/Redo 恢复。
TEST(ModelDocumentTest, CascadeDeleteAndUndoRestoreDependencies)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 20.0}, {"height", 30.0},
    });
    document.createFeature("Sphere", {{"radius", 20.0}});
    document.createFeature("Cylinder", {{"radius", 15.0}, {"height", 40.0}});
    document.createFeature("Box", {
        {"length", 5.0}, {"width", 5.0}, {"height", 5.0},
    }); // Box002 是无关对象，删除后必须保留。

    document.addDependency("Sphere001", "Box001");
    document.addDependency("Cylinder001", "Sphere001");
    EXPECT_EQ(document.deletionOrder("Box001"),
              (std::vector<std::string>{"Cylinder001", "Sphere001", "Box001"}));

    document.deleteFeature("Box001");
    ASSERT_EQ(document.features().size(), 1u);
    EXPECT_EQ(document.features()[0]->id(), "Box002");

    document.undo();
    ASSERT_EQ(document.features().size(), 4u);
    EXPECT_EQ(document.deletionOrder("Box001"),
              (std::vector<std::string>{"Cylinder001", "Sphere001", "Box001"}));

    document.redo();
    ASSERT_EQ(document.features().size(), 1u);
    EXPECT_EQ(document.features()[0]->id(), "Box002");
}

// 验证失败操作既不改变模型，也不向 Undo 栈塞入一条虚假的历史。
TEST(ModelDocumentTest, InvalidOperationDoesNotPolluteHistory)
{
    ModelDocument document;
    document.createFeature("Sphere", {{"radius", 20.0}});

    EXPECT_THROW(
        document.setParameter("Sphere001", "radius", -5.0),
        std::invalid_argument);
    EXPECT_THROW(
        document.setParameter("Sphere001", "height", 10.0),
        std::invalid_argument);
    EXPECT_THROW(document.deleteFeature("missing"), std::invalid_argument);
    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 20.0);

    document.undo(); // 唯一合法历史应是最初的“创建 Sphere”。
    EXPECT_TRUE(document.features().empty()); // 一次撤销就回到空文档。
    EXPECT_FALSE(document.canUndo());         // 三次失败操作都没有增加历史。
}

// 验证 Undo 后发生新修改会形成新时间线，并使旧 Redo 分支失效。
TEST(ModelDocumentTest, NewChangeClearsRedoHistory)
{
    ModelDocument document;
    document.createFeature("Sphere", {{"radius", 20.0}});
    document.setParameter("Sphere001", "radius", 30.0);
    document.undo();               // 从 radius=30 返回 radius=20。
    ASSERT_TRUE(document.canRedo()); // 此时旧修改理论上可以重做。

    document.setParameter("Sphere001", "radius", 40.0); // 从旧状态走出新的分支。
    EXPECT_FALSE(document.canRedo()); // radius=30 的旧未来已不属于当前时间线。
    EXPECT_DOUBLE_EQ(parameterValue(document, "Sphere001", 0), 40.0); // 新值保留。
}

// 失败的创建不能把未登记类型写进序号表，否则 exportData() 产出的数据
// 会连 replaceData() 自己都拒绝，用户永远无法保存这份模型。
TEST(ModelDocumentTest, FailedCreationDoesNotPolluteSequences)
{
    ModelDocument document;
    EXPECT_THROW(document.createFeature("Torus", {}), std::invalid_argument);
    EXPECT_TRUE(document.features().empty()); // 未知类型没有进入文档。
    EXPECT_FALSE(document.canUndo());         // 也没有历史。
    EXPECT_TRUE(document.exportData().sequences.empty()); // 未登记类型没有留下计数。

    // 已登记类型但参数非法时，同样不能消耗序号，否则下一次创建会莫名跳到 Box002。
    EXPECT_THROW(document.createFeature("Box", {{"length", -1.0}}), std::invalid_argument);
    EXPECT_TRUE(document.exportData().sequences.empty());

    // 失败后生成的数据仍是可直接重新打开的空文档。
    ModelDocument fresh;
    EXPECT_NO_THROW(fresh.replaceData(document.exportData()));
    EXPECT_EQ(document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    }).id(), "Box001"); // 失败没有占用 Box 的 001。
}

// 修改参数失败必须原地回滚：旧实现调用 restoreState 重建全部 Feature，
// 会让调用方保存的 Feature* 全部悬空，并且为一次失败付出 O(n) 重建成本。
TEST(ModelDocumentTest, FailedParameterChangeKeepsFeatureAddressAndHistory)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    // Cone 保留一个非零半径；把另一个半径改成 0 会同时为零，
    // 该值能通过 Registry 范围检查，但会被 Feature::validate() 拒绝。
    document.createFeature("Cone", {
        {"radius_bottom", 20.0}, {"radius_top", 0.0}, {"height", 30.0},
    });
    const auto* cone = document.findFeature("Cone001");
    ASSERT_NE(cone, nullptr);
    document.markSaved();

    // Registry 范围检查直接拒绝，根本不会进入修改路径。
    EXPECT_THROW(document.setParameter("Box001", "length", -1.0), std::invalid_argument);
    // 通过范围检查、在 validate 阶段失败：必须只回滚 radius_bottom。
    EXPECT_THROW(document.setParameter("Cone001", "radius_bottom", 0.0), std::invalid_argument);

    EXPECT_EQ(document.findFeature("Cone001"), cone); // 同一对象，说明没有重建 features_。
    EXPECT_DOUBLE_EQ(parameterValue(document, "Cone001", 0), 20.0); // 被改参数恢复旧值。
    EXPECT_DOUBLE_EQ(parameterValue(document, "Box001", 0), 10.0);
    EXPECT_FALSE(document.isModified()); // 失败没有推进 revision。

    document.undo(); // 失败没有新增历史：这次撤销的仍是 Cone 的创建。
    EXPECT_EQ(document.findFeature("Cone001"), nullptr);
    EXPECT_NE(document.findFeature("Box001"), nullptr);
}

// 畸形文件缺导入位置字段时应统一报 invalid_argument，而不是泄漏 at() 的 out_of_range。
TEST(ModelDocumentTest, ReplaceDataRejectsMalformedImportedParametersAsInvalidArgument)
{
    ModelDocument source;
    source.createImportedFeature(forge::geometry::ShapeFactory::makeBox(1, 1, 1), "part.step");
    auto data = source.exportData();
    ASSERT_EQ(data.features.size(), 1u);
    // 保持参数数量为 3，但把 x 换成未知名字：旧实现会从 parameters.at("x") 抛 out_of_range。
    data.features[0].parameters.erase("x");
    data.features[0].parameters.emplace("w", 0.0);

    ModelDocument target;
    EXPECT_THROW(target.replaceData(data), std::invalid_argument);
    EXPECT_TRUE(target.features().empty()); // 校验失败不替换当前文档。
}

// 超长 ID 后缀不能再让 std::stoll 抛 out_of_range，而应作为业务错误统一处理。
TEST(ModelDocumentTest, ReplaceDataRejectsOverflowingIdSuffixAsInvalidArgument)
{
    ModelDocument source;
    source.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    auto data = source.exportData();
    ASSERT_EQ(data.features.size(), 1u);
    // 20 位数字超出 stoll/from_chars 的整数范围，但长度仍满足 ID 格式。
    data.features[0].id = "Box" + std::string(20, '9');

    ModelDocument target;
    EXPECT_THROW(target.replaceData(data), std::invalid_argument);
    EXPECT_TRUE(target.features().empty());
}
