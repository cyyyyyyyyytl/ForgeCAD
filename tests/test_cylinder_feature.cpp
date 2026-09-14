// ============================================================
// CylinderFeature 单元测试
// ------------------------------------------------------------
// 测什么：CylinderFeature 的核心行为（规格与 BoxFeature 测试同构）
//   ① 构造（id / name 对不对）   ② 参数列表
//   ③ 改参数（成功 + 失败抛异常）  ④ 校验（合法 / 非法）
//   ⑤ 重建（返回真实 3D 形状）
// 为什么和 Box 测试长得一样：Cylinder 和 Box 是同一个爸爸的孩子，
//   合同一样 → 验收标准一样 → 测试结构也一样（这就是抽象带来的可预期性）。
// ============================================================
#include <gtest/gtest.h>             // gtest：测试框架（EXPECT_* 断言）
#include <stdexcept>                 // std::invalid_argument：断言"抛异常"用
#include "domain/CylinderFeature.h"  // 被测对象：CylinderFeature

using namespace forge::domain;

// ① 构造测试：id / name 正确（name 必须是爸爸写死的 "Cylinder"）
TEST(CylinderFeatureTest, ConstructorSetsIdAndName) {
    CylinderFeature cyl("Cyl001", 20.0, 60.0);   // 半径 20，高度 60
    EXPECT_EQ(cyl.id(), "Cyl001");
    EXPECT_EQ(cyl.type(), "Cylinder");
}

// ② 参数列表测试：恰好 2 个参数，名字 radius/height，顺序与值正确
TEST(CylinderFeatureTest, ParametersReturnsRadiusHeight) {
    CylinderFeature cyl("Cyl002", 20.0, 60.0);
    const auto& params = cyl.parameters();
    ASSERT_EQ(params.size(), 2);                 // 必须有 2 个参数
    EXPECT_EQ(params[0].name(), "radius");
    EXPECT_EQ(params[1].name(), "height");
    EXPECT_DOUBLE_EQ(params[0].asDouble(), 20.0);
    EXPECT_DOUBLE_EQ(params[1].asDouble(), 60.0);
}

// ③ 改参数（成功）：radius 20 → 30
TEST(CylinderFeatureTest, SetParameterChangesValue) {
    CylinderFeature cyl("Cyl003", 20.0, 60.0);
    cyl.setParameter("radius", 30.0);
    EXPECT_DOUBLE_EQ(cyl.parameters()[0].asDouble(), 30.0);
    EXPECT_DOUBLE_EQ(cyl.parameters()[1].asDouble(), 60.0);   // height 不受影响
}

// ③ 改参数（失败）：Cylinder 没有 "length" 参数 → 必须抛异常
TEST(CylinderFeatureTest, SetUnknownParameterThrows) {
    CylinderFeature cyl("Cyl004", 20.0, 60.0);
    EXPECT_THROW(cyl.setParameter("length", 5.0), std::invalid_argument);
}

// ④ 校验（合法）：正数尺寸 → validate() 返回空串
TEST(CylinderFeatureTest, ValidateAcceptsPositiveDimensions) {
    CylinderFeature cyl("Cyl005", 20.0, 60.0);
    EXPECT_TRUE(cyl.validate().empty());
}

// ④ 校验（非法：负数半径）→ 返回非空原因，且原因里提到是 radius
TEST(CylinderFeatureTest, ValidateRejectsNegativeDimension) {
    CylinderFeature cyl("Cyl006", -5.0, 60.0);
    EXPECT_FALSE(cyl.validate().empty());
    EXPECT_NE(cyl.validate().find("radius"), std::string::npos);
}

// ④ 校验（非法：零高度）→ 返回非空
TEST(CylinderFeatureTest, ValidateRejectsZeroDimension) {
    CylinderFeature cyl("Cyl007", 20.0, 0.0);
    EXPECT_FALSE(cyl.validate().empty());
}

// ⑤ 重建（成功）：正数尺寸 → rebuild() 返回非空真形状
TEST(CylinderFeatureTest, RebuildReturnsValidShape) {
    CylinderFeature cyl("Cyl008", 20.0, 60.0);
    EXPECT_FALSE(cyl.rebuild().IsNull());
}

// ⑤ 重建（失败）：半径改成负数 → rebuild() 返回空形状（ShapeFactory 的失败约定）
TEST(CylinderFeatureTest, RebuildInvalidDimsReturnsNullShape) {
    CylinderFeature cyl("Cyl009", 20.0, 60.0);
    cyl.setParameter("radius", -5.0);
    EXPECT_TRUE(cyl.rebuild().IsNull());
}
