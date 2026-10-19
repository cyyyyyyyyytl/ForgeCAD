#include <gtest/gtest.h> // GoogleTest 的 TEST 与断言宏。

#include "application/ModelDocument.h" // 本文件验证的文档公开 API。
#include "domain/AdvancedFeature.h"    // 读取 AdvancedFeature 的结构化定义。
#include "domain/BooleanFeature.h"     // 布尔创建用例需要运算枚举。
#include "domain/Feature.h"            // 断言需要读取 Feature 的 ID、类型与参数。

#include <BRepGProp.hxx> // 用真实几何体积/质心验证撤销重做后的模型。
#include <GProp_GProps.hxx>

#include <numbers>   // std::numbers::pi 参与圆柱体积断言。
#include <stdexcept> // std::invalid_argument 是失败操作约定的异常类型。
#include <string>
#include <string_view>
#include <vector>

// 这组测试锁定「Command 命令栈」重构后的行为契约：公开 API 不变，但
// Undo/Redo 不再重建整份文档。因此除了数值和几何，测试还会直接比较
// Feature 对象地址，防止实现悄悄退回成整文档快照重建。

using forge::application::ModelDocument;
using forge::domain::AdvancedFeature;
using forge::domain::BooleanOperation;
using forge::domain::FeatureDefinition;

namespace {

// 按稳定 ID 和参数名读取当前数值。找不到对象或参数时先记录失败再返回 0，
// 这样测试不会因为空指针解引用而崩溃，能保留真正有意义的失败信息。
double namedParameter(const ModelDocument& document,
                      std::string_view featureId,
                      const std::string& parameterName)
{
    const auto* feature = document.findFeature(featureId);
    if (feature == nullptr) {
        ADD_FAILURE() << "找不到特征: " << featureId;
        return 0.0;
    }
    for (const auto& parameter : feature->parameters()) {
        if (parameter.name() == parameterName) {
            return parameter.asDouble();
        }
    }
    ADD_FAILURE() << featureId << " 缺少参数: " << parameterName;
    return 0.0;
}

// 文档顺序是观察「撤销创建/级联删除后恢复原位」的窗口。
std::vector<std::string> featureIds(const ModelDocument& document)
{
    std::vector<std::string> ids;
    ids.reserve(document.features().size());
    for (const auto& feature : document.features()) {
        ids.push_back(feature->id());
    }
    return ids;
}

// 只用 OCCT 测量真实几何，避免测试顺着实现里的参数推导“自证正确”。
GProp_GProps volumeProperties(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties;
}

double shapeVolume(const TopoDS_Shape& shape)
{
    return volumeProperties(shape).Mass();
}

} // namespace

// 契约 1：一次成功的参数修改只占一条历史。撤销一次应只回到修改前，
// 再撤销一次才移除更早的创建；如果实现多压了一条历史，第二次断言就会失败。
TEST(CommandHistoryTest, EachSuccessfulEditAddsExactlyOneHistoryEntry)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    document.setParameter("Box001", "length", 20.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "length"), 20.0);

    document.undo(); // 只撤销这一次参数修改。
    ASSERT_NE(document.findFeature("Box001"), nullptr);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "length"), 10.0);
    EXPECT_TRUE(document.canUndo()); // 创建那条历史仍在，说明改参只占一条。

    document.undo(); // 再撤销创建。
    EXPECT_EQ(document.findFeature("Box001"), nullptr);
    EXPECT_FALSE(document.canUndo());
}

// 契约 1（CompositeCommand）：setPosition 一次设置 XYZ 应整体算一条历史。
// 撤销一次必须同时恢复三个坐标，而不是只恢复其中一个再留下两条历史。
TEST(CommandHistoryTest, SetPositionUsesSingleCompositeHistoryEntry)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    const auto before = volumeProperties(document.rebuildShapes().at("Box001"));
    EXPECT_NEAR(before.Mass(), 1000.0, 1e-5); // 10 × 10 × 10。
    EXPECT_NEAR(before.CentreOfMass().X(), 5.0, 1e-6);
    EXPECT_NEAR(before.CentreOfMass().Y(), 5.0, 1e-6);
    EXPECT_NEAR(before.CentreOfMass().Z(), 5.0, 1e-6);

    document.setPosition("Box001", 5.0, 6.0, 7.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "x"), 5.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "y"), 6.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "z"), 7.0);
    const auto moved = volumeProperties(document.rebuildShapes().at("Box001"));
    EXPECT_NEAR(moved.Mass(), 1000.0, 1e-5); // 定位只平移，不改变尺寸。
    EXPECT_NEAR(moved.CentreOfMass().X(), 10.0, 1e-6); // 5 + 10/2。
    EXPECT_NEAR(moved.CentreOfMass().Y(), 11.0, 1e-6);
    EXPECT_NEAR(moved.CentreOfMass().Z(), 12.0, 1e-6);

    document.undo(); // 一次撤销必须同时把 XYZ 全部还原。
    ASSERT_NE(document.findFeature("Box001"), nullptr);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "x"), 0.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "y"), 0.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "z"), 0.0);
    const auto restored = volumeProperties(document.rebuildShapes().at("Box001"));
    EXPECT_NEAR(restored.CentreOfMass().X(), 5.0, 1e-6);
    EXPECT_NEAR(restored.CentreOfMass().Y(), 5.0, 1e-6);
    EXPECT_NEAR(restored.CentreOfMass().Z(), 5.0, 1e-6);
    EXPECT_TRUE(document.canUndo()); // 只剩创建历史，证明定位只占一条。

    document.redo();
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "x"), 5.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "y"), 6.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "z"), 7.0);
}

// 契约 2：参数编辑与位置编辑的 Undo/Redo 必须原地修改同一个 Feature，
// 不能整文档重建。对象地址一变，UI 和 AI 侧长期持有的指针就会悬空。
TEST(CommandHistoryTest, EditUndoRedoKeepsFeatureAddress)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    const auto* box = document.findFeature("Box001");
    ASSERT_NE(box, nullptr);

    document.setParameter("Box001", "width", 20.0);
    EXPECT_EQ(document.findFeature("Box001"), box); // 编辑成功后仍是同一对象。

    document.undo();
    EXPECT_EQ(document.findFeature("Box001"), box); // 撤销也只改参数，不重建。
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "width"), 10.0);

    document.redo();
    EXPECT_EQ(document.findFeature("Box001"), box);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "width"), 20.0);

    document.setPosition("Box001", 1.0, 2.0, 3.0);
    EXPECT_EQ(document.findFeature("Box001"), box);

    document.undo();
    EXPECT_EQ(document.findFeature("Box001"), box);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "x"), 0.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "y"), 0.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "z"), 0.0);

    document.redo();
    EXPECT_EQ(document.findFeature("Box001"), box);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "x"), 1.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "y"), 2.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "z"), 3.0);
}

// 契约 3：撤销创建时特征和它登记的依赖边一起消失；重做恢复同一 ID、
// 类型、依赖和文档顺序，并且几何仍能真实重建（布尔差集体积 875）。
TEST(CommandHistoryTest, CreateUndoRemovesEdgesAndRedoRestoresIdentity)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    document.createFeature("Box", {
        {"length", 5.0}, {"width", 5.0}, {"height", 5.0},
    });
    document.createBooleanFeature(BooleanOperation::Difference, "Box001", "Box002");
    ASSERT_NE(document.findFeature("Cut001"), nullptr);
    const auto orderBefore = featureIds(document);
    EXPECT_EQ(document.dependenciesOf("Cut001"),
              (std::vector<std::string>{"Box001", "Box002"}));
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Cut001")), 875.0, 1e-5);

    document.undo(); // 创建布尔特征只占一条历史，一次撤销移除对象和两条依赖边。
    EXPECT_EQ(document.findFeature("Cut001"), nullptr);
    EXPECT_EQ(featureIds(document), (std::vector<std::string>{"Box001", "Box002"}));
    EXPECT_TRUE(document.canUndo());
    EXPECT_TRUE(document.canRedo());
    EXPECT_THROW(document.dependenciesOf("Cut001"), std::invalid_argument);
    // 图里不能再留下 Box001 指向已删除 Cut001 的下游边。
    EXPECT_EQ(document.deletionOrder("Box001"), (std::vector<std::string>{"Box001"}));
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Box001")), 1000.0, 1e-5);

    document.redo(); // 重做必须插回原 ID、类型、依赖顺序和文档顺序。
    const auto* cut = document.findFeature("Cut001");
    ASSERT_NE(cut, nullptr);
    EXPECT_EQ(cut->id(), "Cut001");
    EXPECT_EQ(cut->type(), "Cut");
    EXPECT_EQ(document.dependenciesOf("Cut001"),
              (std::vector<std::string>{"Box001", "Box002"}));
    EXPECT_EQ(featureIds(document), orderBefore);
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Cut001")), 875.0, 1e-5);
}

// 契约 3（续）：重做创建还必须恢复“参数”。布尔特征没有数值参数，所以
// 这里用带 height 参数的拉伸再验证一次：ID、类型、参数、依赖、顺序、几何
// 全部要回到撤销前，并且“创建 + 登记依赖”只占一条历史。
TEST(CommandHistoryTest, CreateRedoRestoresParametersAndDependency)
{
    ModelDocument document;
    document.createFeature("RectangleSketch", {{"length", 10.0}, {"width", 20.0}});
    document.createExtrudeFeature("RectangleSketch001", 6.0); // 创建和依赖只占一条历史。
    ASSERT_NE(document.findFeature("Extrude001"), nullptr);
    const auto orderBefore = featureIds(document);
    EXPECT_EQ(document.dependenciesOf("Extrude001"),
              (std::vector<std::string>{"RectangleSketch001"}));
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Extrude001")), 1200.0, 1e-5);

    document.undo();
    EXPECT_EQ(document.findFeature("Extrude001"), nullptr);
    ASSERT_NE(document.findFeature("RectangleSketch001"), nullptr); // 草图不能被连带撤销。
    EXPECT_TRUE(document.canUndo()); // 草图创建那条历史仍在。
    // 草图不再有下游，说明拉伸登记的依赖边也跟着一起移除了。
    EXPECT_EQ(document.deletionOrder("RectangleSketch001"),
              (std::vector<std::string>{"RectangleSketch001"}));

    document.redo();
    const auto* extrude = document.findFeature("Extrude001");
    ASSERT_NE(extrude, nullptr);
    EXPECT_EQ(extrude->id(), "Extrude001");
    EXPECT_EQ(extrude->type(), "Extrude");
    EXPECT_DOUBLE_EQ(namedParameter(document, "Extrude001", "height"), 6.0);
    EXPECT_DOUBLE_EQ(namedParameter(document, "Extrude001", "direction"), 0.0);
    EXPECT_EQ(document.dependenciesOf("Extrude001"),
              (std::vector<std::string>{"RectangleSketch001"}));
    EXPECT_EQ(featureIds(document), orderBefore);
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Extrude001")), 1200.0, 1e-5);
}

// 契约 4：级联删除撤销后，被删特征按原文档顺序恢复，依赖关系与删除前一致，
// 真实几何可重新计算；重做再次删除同一组特征。无关的 Box002 必须留在原位。
TEST(CommandHistoryTest, CascadeDeleteUndoRestoresOrderDependenciesAndGeometry)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    document.createFeature("Sphere", {{"radius", 5.0}});
    document.createFeature("Cylinder", {{"radius", 2.0}, {"height", 10.0}});
    document.createFeature("Box", {
        {"length", 4.0}, {"width", 4.0}, {"height", 4.0},
    });
    document.addDependency("Sphere001", "Box001");
    document.addDependency("Cylinder001", "Sphere001");

    const auto orderBefore = featureIds(document);
    const auto sphereDependencies = document.dependenciesOf("Sphere001");
    const auto cylinderDependencies = document.dependenciesOf("Cylinder001");
    const auto deletionOrderBefore = document.deletionOrder("Box001");
    const double boxVolumeBefore = shapeVolume(document.rebuildShapes().at("Box001"));
    const double sphereVolumeBefore = shapeVolume(document.rebuildShapes().at("Sphere001"));
    EXPECT_EQ(deletionOrderBefore,
              (std::vector<std::string>{"Cylinder001", "Sphere001", "Box001"}));

    document.deleteFeature("Box001"); // 整组级联删除只占一条历史。
    EXPECT_EQ(featureIds(document), (std::vector<std::string>{"Box002"}));
    EXPECT_EQ(document.findFeature("Box001"), nullptr);
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr);
    EXPECT_EQ(document.findFeature("Cylinder001"), nullptr);
    EXPECT_TRUE(document.canUndo());

    document.undo(); // 一次撤销恢复全部对象、顺序和依赖边。
    EXPECT_EQ(featureIds(document), orderBefore);
    EXPECT_EQ(document.dependenciesOf("Sphere001"), sphereDependencies);
    EXPECT_EQ(document.dependenciesOf("Cylinder001"), cylinderDependencies);
    EXPECT_EQ(document.deletionOrder("Box001"), deletionOrderBefore);
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Box001")), boxVolumeBefore, 1e-5);
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Sphere001")), sphereVolumeBefore, 1e-5);
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Cylinder001")),
                40.0 * std::numbers::pi, 1e-5); // 半径 2、高 10 的圆柱。
    const auto* unrelated = document.findFeature("Box002");
    ASSERT_NE(unrelated, nullptr); // 与删除目标无关的特征不能受牵连。
    EXPECT_EQ(unrelated->id(), "Box002");

    document.redo(); // 重做必须再次删除整组。
    EXPECT_EQ(featureIds(document), (std::vector<std::string>{"Box002"}));
    EXPECT_EQ(document.findFeature("Box001"), nullptr);
    EXPECT_EQ(document.findFeature("Sphere001"), nullptr);
    EXPECT_EQ(document.findFeature("Cylinder001"), nullptr);
}

// 契约 5：addDependency 只改依赖图。撤销后边消失、两个特征都还在；
// 重做后边按原方向恢复。
TEST(CommandHistoryTest, AddDependencyUndoRemovesEdgeAndRedoRestoresIt)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    document.createFeature("Sphere", {{"radius", 3.0}});
    document.addDependency("Sphere001", "Box001");
    EXPECT_EQ(document.dependenciesOf("Sphere001"),
              (std::vector<std::string>{"Box001"}));

    document.undo(); // 只撤销这条边，不能连特征一起撤销。
    EXPECT_EQ(document.dependenciesOf("Sphere001"), (std::vector<std::string>{}));
    EXPECT_NE(document.findFeature("Box001"), nullptr);
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);
    EXPECT_TRUE(document.canRedo());

    document.redo();
    EXPECT_EQ(document.dependenciesOf("Sphere001"),
              (std::vector<std::string>{"Box001"}));
}

// 契约 6：非法类型、非法参数、重复/成环依赖、删除不存在对象都抛
// invalid_argument，且不产生历史、不改变保存点状态、不污染依赖图。
TEST(CommandHistoryTest, FailedOperationsDoNotEnterHistoryOrChangeSaveState)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    document.createFeature("Sphere", {{"radius", 3.0}});
    document.addDependency("Sphere001", "Box001");
    document.markSaved(); // 以当前状态作为保存点，失败操作不能让它变成“已修改”。

    const bool canUndoBefore = document.canUndo();
    const bool canRedoBefore = document.canRedo();

    EXPECT_THROW(document.createFeature("Torus", {}), std::invalid_argument);
    EXPECT_THROW(document.setParameter("Box001", "length", -1.0), std::invalid_argument);
    EXPECT_THROW(document.setParameter("Box001", "unknown", 5.0), std::invalid_argument);
    // 位置超出 ±1000000 的范围：必须在写值前拒绝。
    EXPECT_THROW(document.setPosition("Sphere001", 0.0, 1000001.0, 0.0),
                 std::invalid_argument);
    EXPECT_THROW(document.addDependency("Sphere001", "Box001"), std::invalid_argument);
    EXPECT_THROW(document.addDependency("Box001", "Sphere001"), std::invalid_argument);
    EXPECT_THROW(document.deleteFeature("missing"), std::invalid_argument);

    EXPECT_EQ(document.canUndo(), canUndoBefore);
    EXPECT_EQ(document.canRedo(), canRedoBefore);
    EXPECT_FALSE(document.isModified());
    EXPECT_EQ(document.dependenciesOf("Sphere001"),
              (std::vector<std::string>{"Box001"})); // 拒绝成环后原图不变。

    document.undo(); // 失败操作没有占用历史：这次撤销的仍是最近一次合法操作。
    EXPECT_EQ(document.dependenciesOf("Sphere001"), (std::vector<std::string>{}));
    EXPECT_TRUE(document.canUndo());
}

// 契约 7：保存点跟着 revision 走。撤销回保存点 isModified=false，重做离开
// 保存点 isModified=true；撤销后做新修改会清空旧 Redo 分支。
TEST(CommandHistoryTest, SavePointFollowsUndoRedoAndNewChangeClearsRedo)
{
    ModelDocument document;
    document.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    document.markSaved();
    EXPECT_FALSE(document.isModified());

    document.setParameter("Box001", "length", 20.0);
    EXPECT_TRUE(document.isModified());
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Box001")), 2000.0, 1e-5);

    document.undo(); // 回到保存点。
    EXPECT_FALSE(document.isModified());
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "length"), 10.0);
    EXPECT_TRUE(document.canRedo());

    document.redo(); // 离开保存点。
    EXPECT_TRUE(document.isModified());
    EXPECT_DOUBLE_EQ(namedParameter(document, "Box001", "length"), 20.0);

    document.undo();
    EXPECT_FALSE(document.isModified());
    document.setParameter("Box001", "width", 30.0); // 从保存点走出新的时间线。
    EXPECT_FALSE(document.canRedo()); // 旧的 redo 分支必须被丢弃。
    EXPECT_TRUE(document.isModified());
}

// 契约 8：replaceData 表示“打开另一份文档”，必须清空会话历史并重置保存点；
// 替换之后的新修改仍能正常撤销，说明历史从新的基线重新开始。
TEST(CommandHistoryTest, ReplaceDataClearsHistoryAndResetsSavePoint)
{
    ModelDocument source;
    source.createFeature("Box", {
        {"length", 10.0}, {"width", 10.0}, {"height", 10.0},
    });
    source.createFeature("Sphere", {{"radius", 5.0}});
    const auto data = source.exportData();

    ModelDocument document;
    document.createFeature("Cylinder", {{"radius", 2.0}, {"height", 10.0}});
    document.setParameter("Cylinder001", "radius", 3.0);
    ASSERT_TRUE(document.canUndo());

    document.replaceData(data);
    EXPECT_FALSE(document.canUndo());
    EXPECT_FALSE(document.canRedo());
    EXPECT_FALSE(document.isModified());
    EXPECT_EQ(featureIds(document), (std::vector<std::string>{"Box001", "Sphere001"}));
    EXPECT_NEAR(shapeVolume(document.rebuildShapes().at("Box001")), 1000.0, 1e-5);

    document.setParameter("Box001", "length", 20.0); // 新历史从替换后的保存点开始。
    EXPECT_TRUE(document.canUndo());
    EXPECT_TRUE(document.isModified());
    document.undo();
    EXPECT_FALSE(document.isModified());
    EXPECT_FALSE(document.canUndo()); // replaceData 之前的历史确实没有残留。
}

// 契约 9：空历史上的 undo/redo 是安静的安全 no-op，不能抛异常，
// 也不能破坏另一条栈（例如空 Undo 不能吞掉仍然可用的 Redo）。
TEST(CommandHistoryTest, UndoRedoOnEmptyStacksAreSafeNoOps)
{
    ModelDocument document;
    EXPECT_FALSE(document.canUndo());
    EXPECT_FALSE(document.canRedo());
    EXPECT_NO_THROW(document.undo());
    EXPECT_NO_THROW(document.redo());
    EXPECT_TRUE(document.features().empty());
    EXPECT_FALSE(document.isModified());

    document.createFeature("Sphere", {{"radius", 2.0}});
    document.undo();
    ASSERT_FALSE(document.canUndo());
    ASSERT_TRUE(document.canRedo());
    ASSERT_TRUE(document.features().empty());

    EXPECT_NO_THROW(document.undo()); // 空 Undo 栈的无效撤销不能破坏 Redo。
    EXPECT_TRUE(document.canRedo());
    EXPECT_TRUE(document.features().empty());

    document.redo();
    ASSERT_NE(document.findFeature("Sphere001"), nullptr);
    ASSERT_FALSE(document.canRedo());
    ASSERT_TRUE(document.canUndo());

    EXPECT_NO_THROW(document.redo()); // 空 Redo 栈的无效重做不能改变文档。
    EXPECT_FALSE(document.canRedo());
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);
}

// 契约 10：AdvancedFeature 的定义编辑（ProfileSketch 轮廓顶点）也走同一套
// 命令历史。Undo 恢复旧 vertices，Redo 恢复新 vertices，且 Feature 地址不变。
TEST(CommandHistoryTest, AdvancedDefinitionEditUndoRedoKeepsFeatureAddress)
{
    ModelDocument document;
    FeatureDefinition definition;
    // 4 个闭合顶点、plane=0、closed=1：在 XY 平面画一个矩形轮廓。
    definition.vertices = {
        {0.0, 0.0, 0.0}, {20.0, 0.0, 0.0}, {20.0, 20.0, 0.0}, {0.0, 20.0, 0.0},
    };
    document.createAdvancedFeature(
        "ProfileSketch", {{"plane", 0.0}, {"closed", 1.0}}, definition);

    const auto* sketch = document.findFeature("ProfileSketch001");
    ASSERT_NE(sketch, nullptr);
    const auto* advanced = dynamic_cast<const AdvancedFeature*>(sketch);
    ASSERT_NE(advanced, nullptr);
    ASSERT_EQ(advanced->definition().vertices.size(), 4u);
    EXPECT_DOUBLE_EQ(advanced->definition().vertices[1].u, 20.0);
    EXPECT_FALSE(document.rebuildShapes().at("ProfileSketch001").IsNull());

    FeatureDefinition changed = definition;
    changed.vertices[1].u = 40.0; // 把右上角向外拉，仍是合法闭合轮廓。
    document.setAdvancedDefinition(
        "ProfileSketch001", {{"plane", 0.0}, {"closed", 1.0}}, changed);

    EXPECT_EQ(document.findFeature("ProfileSketch001"), sketch); // 编辑不重建对象。
    ASSERT_EQ(advanced->definition().vertices.size(), 4u);
    EXPECT_DOUBLE_EQ(advanced->definition().vertices[1].u, 40.0);

    document.undo(); // 撤销定义编辑，恢复旧 vertices。
    ASSERT_EQ(document.findFeature("ProfileSketch001"), sketch); // 撤销后仍是同一对象。
    ASSERT_EQ(advanced->definition().vertices.size(), 4u);
    EXPECT_DOUBLE_EQ(advanced->definition().vertices[1].u, 20.0);
    EXPECT_FALSE(document.rebuildShapes().at("ProfileSketch001").IsNull());

    document.redo(); // 重做定义编辑，恢复新 vertices。
    ASSERT_EQ(document.findFeature("ProfileSketch001"), sketch);
    ASSERT_EQ(advanced->definition().vertices.size(), 4u);
    EXPECT_DOUBLE_EQ(advanced->definition().vertices[1].u, 40.0);
}
