#include <gtest/gtest.h> // GoogleTest 的 TEST、EXPECT_* 和 ASSERT_* 宏。

#include "application/ModelDocument.h" // 本文件直接验证的文档 API。
#include "domain/Feature.h"             // 断言需要读取查找到的 Feature 参数。

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

} // namespace

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
