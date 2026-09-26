// ============================================================
// SphereFeature 单元测试
// ------------------------------------------------------------
// 测什么：SphereFeature 的核心行为（与 Box/Cylinder 测试同构）
//   ① 构造（id / name）  ② 参数列表（radius 和球心位置）
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
    EXPECT_EQ(sph.id(), "Sph001");      // 构造参数中的稳定 ID 必须原样保留。
    EXPECT_EQ(sph.type(), "Sphere");    // 类型名由 SphereFeature 固定为 Sphere。
}

// ② 参数列表测试：半径在前，球心 x/y/z 在后
TEST(SphereFeatureTest, ParametersReturnsRadius) {
    SphereFeature sph("Sph002", 20.0);
    const auto& params = sph.parameters(); // const 引用避免复制参数 vector。
    ASSERT_EQ(params.size(), 4u);            // 半径与三个球心位置参数
    EXPECT_EQ(params[0].name(), "radius");          // 半径参数的协议名必须正确。
    EXPECT_DOUBLE_EQ(params[0].asDouble(), 20.0);    // 构造时半径值必须正确保存。
}

// ③ 改参数（成功）：radius 20 → 30
TEST(SphereFeatureTest, SetParameterChangesValue) {
    SphereFeature sph("Sph003", 20.0);                 // 初始半径为 20。
    sph.setParameter("radius", 30.0);                   // 通过统一接口改为 30。
    EXPECT_DOUBLE_EQ(sph.parameters()[0].asDouble(), 30.0); // 新值应立即可读。
}

// ③ 改参数（失败）：Sphere 没有 "length" 参数 → 必须抛异常
TEST(SphereFeatureTest, SetUnknownParameterThrows) {
    SphereFeature sph("Sph004", 20.0); // 建立一个合法对象作为失败操作前状态。
    EXPECT_THROW(sph.setParameter("length", 5.0), std::invalid_argument);
}

// ④ 校验（合法）：正数半径 → validate() 返回空串
TEST(SphereFeatureTest, ValidateAcceptsPositiveDimensions) {
    SphereFeature sph("Sph005", 20.0); // 正半径满足球体领域条件。
    EXPECT_TRUE(sph.validate().empty()); // 空错误字符串表示校验通过。
}

// ④ 校验（非法：负数半径）→ 返回非空原因，且提到是 radius
TEST(SphereFeatureTest, ValidateRejectsNegativeDimension) {
    SphereFeature sph("Sph006", -5.0); // 负半径是非法状态。
    EXPECT_FALSE(sph.validate().empty()); // 必须返回非空错误原因。
    EXPECT_NE(sph.validate().find("radius"), std::string::npos); // 原因应指出参数名。
}

// ④ 校验（非法：零半径）→ 返回非空
TEST(SphereFeatureTest, ValidateRejectsZeroDimension) {
    SphereFeature sph("Sph007", 0.0); // 零半径会退化为点。
    EXPECT_FALSE(sph.validate().empty()); // 退化形状不能通过校验。
}

// ⑤ 重建（成功）：正数半径 → rebuild() 返回非空真形状
TEST(SphereFeatureTest, RebuildReturnsValidShape) {
    SphereFeature sph("Sph008", 20.0); // 使用合法半径构建领域对象。
    EXPECT_FALSE(sph.rebuild().IsNull()); // ShapeFactory 应返回非空 OCCT 球体。
}

// ⑤ 重建（失败）：半径改成负数 → rebuild() 返回空形状（ShapeFactory 的失败约定）
TEST(SphereFeatureTest, RebuildInvalidDimsReturnsNullShape) {
    SphereFeature sph("Sph009", 20.0); // 先建立合法球体。
    sph.setParameter("radius", -5.0);   // 故意绕过 Document 范围检查制造非法状态。
    EXPECT_TRUE(sph.rebuild().IsNull()); // 几何层最后一道防御应返回空 Shape。
}
