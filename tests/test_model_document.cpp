#include <gtest/gtest.h>

#include "application/ModelDocument.h"
#include "domain/FeatureFactory.h"

using forge::application::ModelDocument;
using forge::domain::FeatureFactory;

// ============================================================
// ModelDocumentTest：验证模型所有权、稳定 ID 和查找契约
// ------------------------------------------------------------
// 这些能力会被 UI、Command、文件导入和 AI 同时依赖，所以测试只针对
// 纯 C++ Document，不需要启动 Qt 窗口或 OCCT Viewer。
// ============================================================

// 同类特征连续编号，不同类型各自从 001 开始。
TEST(ModelDocumentTest, GeneratesIndependentStableIdsByType)
{
    ModelDocument document;
    EXPECT_EQ(document.nextFeatureId("Box"), "Box001");
    EXPECT_EQ(document.nextFeatureId("Box"), "Box002");
    EXPECT_EQ(document.nextFeatureId("Sphere"), "Sphere001");
}

// 导入场景可能先加入一个带现成 ID 的 Feature；生成器必须跳过冲突 ID。
TEST(ModelDocumentTest, GeneratedIdSkipsAnExistingImportedId)
{
    ModelDocument document;
    document.addFeature(FeatureFactory::create("Box", "Box001", {10.0, 20.0, 30.0}));

    EXPECT_EQ(document.nextFeatureId("Box"), "Box002");
}

// addFeature 应接管 unique_ptr 所有权，并能通过稳定 ID 找回同一个堆对象。
TEST(ModelDocumentTest, OwnsAndFindsFeaturesById)
{
    ModelDocument document;
    auto& added = document.addFeature(
        FeatureFactory::create("Sphere", "Sphere001", {20.0}));

    // 返回引用、容器元素和 findFeature 三者必须指向同一个 Feature。
    EXPECT_EQ(added.id(), "Sphere001");
    EXPECT_EQ(document.features().size(), 1u);
    EXPECT_EQ(document.findFeature("Sphere001"), &added);
    EXPECT_EQ(document.findFeature("missing"), nullptr);
}

// 空指针会造成遍历崩溃，重复 ID 会破坏定位语义，两者都必须拒绝。
TEST(ModelDocumentTest, RejectsNullAndDuplicateFeatures)
{
    // 第一层防御：Document 中不允许出现空 Feature 槽位。
    ModelDocument document;
    EXPECT_THROW(document.addFeature(nullptr), std::invalid_argument);

    // 第二层防御：同一文档内的 Feature ID 必须唯一。
    document.addFeature(FeatureFactory::create("Sphere", "Sphere001", {20.0}));
    EXPECT_THROW(
        document.addFeature(FeatureFactory::create("Sphere", "Sphere001", {30.0})),
        std::invalid_argument);
}

// removeFeature 不销毁对象，而是把 unique_ptr 所有权从 Document 交还给调用方。
TEST(ModelDocumentTest, RemovesFeatureAndReturnsItsOwnership)
{
    ModelDocument document;
    auto& added = document.addFeature(
        FeatureFactory::create("Box", "Box001", {10.0, 20.0, 30.0}));
    const auto* originalAddress = &added;

    // 移除后文档不再包含 Box001，但返回的 unique_ptr 仍拥有原来的堆对象。
    auto removed = document.removeFeature("Box001");
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed.get(), originalAddress);
    EXPECT_EQ(document.findFeature("Box001"), nullptr);
    EXPECT_TRUE(document.features().empty());

    // 把所有权交还给文档后，仍是同一个对象；这正是 Redo 需要的能力。
    auto& restored = document.addFeature(std::move(removed));
    EXPECT_EQ(&restored, originalAddress);
    EXPECT_EQ(document.findFeature("Box001"), originalAddress);
}

TEST(ModelDocumentTest, RemovingMissingFeatureLeavesDocumentUnchanged)
{
    ModelDocument document;
    document.addFeature(
        FeatureFactory::create("Sphere", "Sphere001", {20.0}));

    // 未命中的删除返回空指针，也不能误删其他 Feature。
    auto removed = document.removeFeature("missing");
    EXPECT_EQ(removed, nullptr);
    EXPECT_EQ(document.features().size(), 1u);
    EXPECT_NE(document.findFeature("Sphere001"), nullptr);
}
