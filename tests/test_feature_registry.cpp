#include <gtest/gtest.h> // GoogleTest 测试与断言宏。

#include "domain/Feature.h"         // unique_ptr 解引用后读取 Feature 并调用 rebuild。
#include "domain/FeatureRegistry.h" // 本文件验证的登记、查找、创建接口。

using forge::domain::FeatureRegistry; // 缩短静态 Registry 调用。
using forge::domain::NumericParameters; // 记录被测具名参数 Map 类型。

// 验证登记表包含全部三种类型，并能正确描述 Box 参数与未知类型。
TEST(FeatureRegistryTest, DescribesAllSupportedFeatures)
{
    EXPECT_EQ(FeatureRegistry::all().size(), 3u); // 当前产品合同明确支持三类基础体。

    const auto* box = FeatureRegistry::find("Box"); // 借用登记表中的只读 Box 描述。
    ASSERT_NE(box, nullptr);                         // 后续解引用前必须保证查找成功。
    ASSERT_EQ(box->parameters.size(), 6u);           // Box 包含长宽高与三个位置参数。
    EXPECT_EQ(box->parameters[0].name, "length");  // 参数顺序决定构造 values 下标。
    EXPECT_NE(FeatureRegistry::find("Cylinder"), nullptr); // Cylinder 已登记。
    EXPECT_NE(FeatureRegistry::find("Sphere"), nullptr);   // Sphere 已登记。
    EXPECT_EQ(FeatureRegistry::find("Torus"), nullptr);    // Torus 尚未支持。
}

// 验证 unordered_map 输入即使顺序打乱，也会按 Descriptor 顺序创建正确 Box。
TEST(FeatureRegistryTest, CreatesFeatureFromNamedParameters)
{
    auto feature = FeatureRegistry::create("Box", "Box001", {
        {"height", 30.0},
        {"length", 100.0},
        {"width", 50.0},
    });

    ASSERT_NE(feature, nullptr);                   // 工厂必须交回有效 unique_ptr。
    EXPECT_EQ(feature->id(), "Box001");          // 调用方给出的稳定 ID 被保留。
    EXPECT_EQ(feature->type(), "Box");           // 实际创建的是 BoxFeature 多态对象。
    EXPECT_FALSE(feature->rebuild().IsNull());    // 参数顺序正确，几何能够成功重建。
}

// 验证三类输入错误都在对象创建前被 Registry 明确拒绝。
TEST(FeatureRegistryTest, RejectsUnknownMissingAndOutOfRangeValues)
{
    // 未登记 Torus，即使提供 radius 也不能创建。
    EXPECT_THROW(
        FeatureRegistry::create("Torus", "Torus001", {{"radius", 10.0}}),
        std::invalid_argument);
    // Sphere 要求 radius，错误名字 wrong 应被识别为缺少必填参数。
    EXPECT_THROW(
        FeatureRegistry::create("Sphere", "Sphere001", {{"wrong", 20.0}}),
        std::invalid_argument);
    // 已知参数的值低于 Descriptor 最小值，同样抛 invalid_argument。
    EXPECT_THROW(
        FeatureRegistry::create("Sphere", "Sphere001", {{"radius", -1.0}}),
        std::invalid_argument);
}
