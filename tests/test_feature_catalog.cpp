#include <gtest/gtest.h>

#include "domain/FeatureCatalog.h"
#include "domain/FeatureFactory.h"

using namespace forge::domain;

// ============================================================
// FeatureCatalogTest：验证“参数规则唯一来源”没有登记错误
// ------------------------------------------------------------
// Catalog 同时驱动 UI、Factory 和 AI Schema，一处错误会影响三条链路，
// 因此测试既检查支持类型，也检查参数数量、顺序和默认值。
// ============================================================

// 三种当前支持的 Feature 必须能查到；尚未实现的 Torus 必须返回 nullptr。
TEST(FeatureCatalogTest, ContainsCurrentFeatureSchemas)
{
    // Box 是参数最多的现有类型，用它核对参数顺序和默认值。
    const auto* box = FeatureCatalog::find("Box");
    ASSERT_NE(box, nullptr);
    ASSERT_EQ(box->parameters.size(), 3u);
    EXPECT_EQ(box->parameters[0].name, "length");
    EXPECT_DOUBLE_EQ(box->parameters[0].defaultValue, 100.0);

    EXPECT_NE(FeatureCatalog::find("Cylinder"), nullptr);
    EXPECT_NE(FeatureCatalog::find("Sphere"), nullptr);
    EXPECT_EQ(FeatureCatalog::find("Torus"), nullptr);
}

// unordered_map 遍历顺序不稳定；具名 Factory 必须按 Catalog 顺序重排，
// 否则 height/length/width 的输入顺序变化会生成尺寸错误的盒子。
TEST(FeatureCatalogTest, NamedFactoryParametersDoNotDependOnMapOrder)
{
    // 故意使用与 Box 构造函数不同的插入顺序，暴露潜在的下标依赖。
    NumericParameters parameters = {
        {"height", 30.0},
        {"length", 100.0},
        {"width", 50.0},
    };

    // 最终 Feature 参数仍应严格排列为 length=100、width=50、height=30。
    auto feature = FeatureFactory::createNamed("Box", "Box001", parameters);
    ASSERT_NE(feature, nullptr);
    EXPECT_DOUBLE_EQ(feature->parameters()[0].asDouble(), 100.0);
    EXPECT_DOUBLE_EQ(feature->parameters()[1].asDouble(), 50.0);
    EXPECT_DOUBLE_EQ(feature->parameters()[2].asDouble(), 30.0);
}

// AI 可能漏字段、幻觉字段或给出越界值，三种输入都必须在对象创建前被拒绝。
TEST(FeatureCatalogTest, NamedFactoryRejectsMissingUnknownAndOutOfRangeParameters)
{
    // Box 缺少 height：参数数量不完整。
    EXPECT_THROW(
        FeatureFactory::createNamed("Box", "Box001", NumericParameters{
            {"length", 100.0}, {"width", 50.0}}),
        std::invalid_argument);

    // Sphere 只认识 radius，diameter 属于未登记参数。
    EXPECT_THROW(
        FeatureFactory::createNamed("Sphere", "Sphere001", NumericParameters{
            {"diameter", 20.0}}),
        std::invalid_argument);

    // radius 为负数，违反 Catalog 的最小值约束。
    EXPECT_THROW(
        FeatureFactory::createNamed("Sphere", "Sphere001", NumericParameters{
            {"radius", -1.0}}),
        std::invalid_argument);
}
