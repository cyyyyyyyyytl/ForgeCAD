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
    EXPECT_EQ(cyl.id(), "Cyl001");       // 稳定 ID 必须保留构造输入。
    EXPECT_EQ(cyl.type(), "Cylinder");   // 子类类型名固定为 Cylinder。
}

// ② 参数列表测试：尺寸 radius/height 在前，位置 x/y/z 在后
TEST(CylinderFeatureTest, ParametersReturnsRadiusHeight) {
    CylinderFeature cyl("Cyl002", 20.0, 60.0);
    const auto& params = cyl.parameters();        // 只读引用避免复制参数列表。
    ASSERT_EQ(params.size(), 5u);                 // 两个尺寸与三个位置参数
    EXPECT_EQ(params[0].name(), "radius");       // 下标 0 固定为半径。
    EXPECT_EQ(params[1].name(), "height");       // 下标 1 固定为高度。
    EXPECT_DOUBLE_EQ(params[0].asDouble(), 20.0); // 半径初始值正确。
    EXPECT_DOUBLE_EQ(params[1].asDouble(), 60.0); // 高度初始值正确。
}

// ③ 改参数（成功）：radius 20 → 30
TEST(CylinderFeatureTest, SetParameterChangesValue) {
    CylinderFeature cyl("Cyl003", 20.0, 60.0); // 建立合法圆柱。
    cyl.setParameter("radius", 30.0);           // 只修改半径。
    EXPECT_DOUBLE_EQ(cyl.parameters()[0].asDouble(), 30.0); // 新半径生效。
    EXPECT_DOUBLE_EQ(cyl.parameters()[1].asDouble(), 60.0);   // height 不受影响
}

// ③ 改参数（失败）：Cylinder 没有 "length" 参数 → 必须抛异常
TEST(CylinderFeatureTest, SetUnknownParameterThrows) {
    CylinderFeature cyl("Cyl004", 20.0, 60.0); // 为错误参数名准备合法对象。
    EXPECT_THROW(cyl.setParameter("length", 5.0), std::invalid_argument);
}

// ④ 校验（合法）：正数尺寸 → validate() 返回空串
TEST(CylinderFeatureTest, ValidateAcceptsPositiveDimensions) {
    CylinderFeature cyl("Cyl005", 20.0, 60.0); // 半径和高度都为正。
    EXPECT_TRUE(cyl.validate().empty());         // 空字符串表示合法。
}

// ④ 校验（非法：负数半径）→ 返回非空原因，且原因里提到是 radius
TEST(CylinderFeatureTest, ValidateRejectsNegativeDimension) {
    CylinderFeature cyl("Cyl006", -5.0, 60.0); // 负半径非法。
    EXPECT_FALSE(cyl.validate().empty());         // 必须产生错误原因。
    EXPECT_NE(cyl.validate().find("radius"), std::string::npos); // 指出 radius。
}

// ④ 校验（非法：零高度）→ 返回非空
TEST(CylinderFeatureTest, ValidateRejectsZeroDimension) {
    CylinderFeature cyl("Cyl007", 20.0, 0.0); // 零高度会退化。
    EXPECT_FALSE(cyl.validate().empty());       // 退化圆柱不合法。
}

// ⑤ 重建（成功）：正数尺寸 → rebuild() 返回非空真形状
TEST(CylinderFeatureTest, RebuildReturnsValidShape) {
    CylinderFeature cyl("Cyl008", 20.0, 60.0); // 合法尺寸。
    EXPECT_FALSE(cyl.rebuild().IsNull());        // 应生成非空 OCCT 圆柱体。
}

// ⑤ 重建（失败）：半径改成负数 → rebuild() 返回空形状（ShapeFactory 的失败约定）
TEST(CylinderFeatureTest, RebuildInvalidDimsReturnsNullShape) {
    CylinderFeature cyl("Cyl009", 20.0, 60.0); // 先建立合法状态。
    cyl.setParameter("radius", -5.0);           // 故意制造非法半径。
    EXPECT_TRUE(cyl.rebuild().IsNull());         // 几何层应安全返回空形状。
}
