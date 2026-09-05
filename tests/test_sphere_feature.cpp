// ============================================================
// SphereFeature 单元测试
// ------------------------------------------------------------
// 测什么：SphereFeature 的核心行为（与 Box/Cylinder 测试同构）
//   ① 构造（id / name）  ② 参数列表（只有 radius 一个）
//   ③ 改参数（成功 + 失败抛异常）  ④ 校验（合法 / 非法）
//   ⑤ 重建（返回真实 3D 形状）
// 三个特征测试长得一样 = 抽象带来的可预期性：合同一致 → 验收标准一致
// ============================================================
#include <gtest/gtest.h>             // gtest：测试框架（EXPECT_* 断言）
#include <stdexcept>                 // std::invalid_argument：断言"抛异常"用
#include "domain/SphereFeature.h"    // 被测对象：SphereFeature

using namespace forge::domain;

// ① 构造测试：id / name 正确（name 必须是爸爸写死的 "Sphere"）
TEST(SphereFeatureTest, ConstructorSetsIdAndName) {
    SphereFeature sph("Sph001", 20.0);   // 半径 20
    EXPECT_EQ(sph.id(), "Sph001");
    EXPECT_EQ(sph.name(), "Sphere");
}

// ② 参数列表测试：恰好 1 个参数，名字 radius
TEST(SphereFeatureTest, ParametersReturnsRadius) {
    SphereFeature sph("Sph002", 20.0);
    const auto& params = sph.parameters();
    ASSERT_EQ(params.size(), 1u);            // 球体只有 1 个参数
    EXPECT_EQ(params[0].name(), "radius");
    EXPECT_DOUBLE_EQ(params[0].asDouble(), 20.0);
}

// ③ 改参数（成功）：radius 20 → 30
TEST(SphereFeatureTest, SetParameterChangesValue) {
    SphereFeature sph("Sph003", 20.0);
    sph.setParameter("radius", 30.0);
    EXPECT_DOUBLE_EQ(sph.parameters()[0].asDouble(), 30.0);
}

// ③ 改参数（失败）：Sphere 没有 "length" 参数 → 必须抛异常
TEST(SphereFeatureTest, SetUnknownParameterThrows) {
    SphereFeature sph("Sph004", 20.0);
    EXPECT_THROW(sph.setParameter("length", 5.0), std::invalid_argument);
}

// ④ 校验（合法）：正数半径 → validate() 返回空串
TEST(SphereFeatureTest, ValidateAcceptsPositiveDimensions) {
    SphereFeature sph("Sph005", 20.0);
    EXPECT_TRUE(sph.validate().empty());
}

// ④ 校验（非法：负数半径）→ 返回非空原因，且提到是 radius
TEST(SphereFeatureTest, ValidateRejectsNegativeDimension) {
    SphereFeature sph("Sph006", -5.0);
    EXPECT_FALSE(sph.validate().empty());
    EXPECT_NE(sph.validate().find("radius"), std::string::npos);
}

// ④ 校验（非法：零半径）→ 返回非空
TEST(SphereFeatureTest, ValidateRejectsZeroDimension) {
    SphereFeature sph("Sph007", 0.0);
    EXPECT_FALSE(sph.validate().empty());
}

// ⑤ 重建（成功）：正数半径 → rebuild() 返回非空真形状
TEST(SphereFeatureTest, RebuildReturnsValidShape) {
    SphereFeature sph("Sph008", 20.0);
    EXPECT_FALSE(sph.rebuild().IsNull());
}

// ⑤ 重建（失败）：半径改成负数 → rebuild() 返回空形状（ShapeFactory 的失败约定）
TEST(SphereFeatureTest, RebuildInvalidDimsReturnsNullShape) {
    SphereFeature sph("Sph009", 20.0);
    sph.setParameter("radius", -5.0);
    EXPECT_TRUE(sph.rebuild().IsNull());
}
