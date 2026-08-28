// ============================================================
// BoxFeature 单元测试
// ------------------------------------------------------------
// 测什么：BoxFeature 的 5 个核心行为（日志挑战③的规格）
//   ① 构造（id / name 对不对）   ② 参数列表
//   ③ 改参数（成功 + 失败抛异常）  ④ 校验（合法 / 非法）
//   ⑤ 重建（返回描述文本）
// 测试即"规格书"：这些断言就是 BoxFeature 的合同，
//   以后改代码把测试跑红 = 破坏了合同。
// ============================================================
#include <gtest/gtest.h>             // gtest：测试框架（EXPECT_* 断言）
#include <stdexcept>                 // std::invalid_argument：断言"抛异常"用
#include "domain/BoxFeature.h"       // 被测对象：我们写的 BoxFeature

using namespace forge::domain;      // 省得每个类型前都写 forge::domain::

// ------------------------------------------------------------
// ① 构造测试
// ------------------------------------------------------------
// 目标：建一个 BoxFeature，确认它"认识自己"（id / name 正确）
TEST(BoxFeatureTest, ConstructorSetsIdAndName) {
    BoxFeature box("Box001", 100, 50, 30);   // 建一个 100x50x30 的盒子
    EXPECT_EQ(box.id(), "Box001");           // id 必须是构造时给的 "Box001"
    EXPECT_EQ(box.name(), "Box");            // name 必须是 "Box"（爸爸 Feature 里写死的）
}

// ------------------------------------------------------------
// ② 参数列表测试
// ------------------------------------------------------------
// 目标：parameters() 必须返回 3 个参数，名字按 length/width/height 顺序
TEST(BoxFeatureTest, ParametersReturnsLengthWidthHeight) {
    BoxFeature box("Box002", 10, 20, 30);    // 注意构造顺序：length=10, width=20, height=30
    const auto& params = box.parameters();   // 拿到参数列表（const 引用，不拷贝）
    ASSERT_EQ(params.size(), 3);             // 断言：必须有 3 个参数
                                             //  （ASSERT_EQ 失败=直接停；EXPECT_EQ 失败=继续跑）
    EXPECT_EQ(params[0].name(), "length");   // 第 0 个叫 length
    EXPECT_EQ(params[1].name(), "width");    // 第 1 个叫 width
    EXPECT_EQ(params[2].name(), "height");   // 第 2 个叫 height
    // 顺便验证值也对：10 / 20 / 30
    EXPECT_DOUBLE_EQ(params[0].asDouble(), 10.0);
    EXPECT_DOUBLE_EQ(params[1].asDouble(), 20.0);
    EXPECT_DOUBLE_EQ(params[2].asDouble(), 30.0);
}

// ------------------------------------------------------------
// ③ 改参数测试（成功路径）
// ------------------------------------------------------------
// 目标：把 length 改成 200 后，参数列表里第 0 个的值必须变
TEST(BoxFeatureTest, SetParameterChangesValue) {
    BoxFeature box("Box003", 100, 50, 30);
    box.setParameter("length", 200.0);       // 调我们的 setParameter
    const auto& params = box.parameters();
    EXPECT_DOUBLE_EQ(params[0].asDouble(), 200.0);  // length 从 100 → 200
    EXPECT_DOUBLE_EQ(params[1].asDouble(), 50.0);   // 其他参数不受影响
}

// ------------------------------------------------------------
// ③ 改参数测试（失败路径：改不存在的参数必须抛异常）
// ------------------------------------------------------------
// 目标：Box 没有 "radius" 这个参数，改它必须抛 std::invalid_argument
//   EXPECT_THROW(语句, 异常类型) = "这句必须抛指定异常，否则测试失败"
TEST(BoxFeatureTest, SetUnknownParameterThrows) {
    BoxFeature box("Box004", 100, 50, 30);
    EXPECT_THROW(box.setParameter("radius", 5.0), std::invalid_argument);
}

// ------------------------------------------------------------
// ④ 校验测试（合法）
// ------------------------------------------------------------
// 目标：正数尺寸 = 合法，validate() 返回空串 ""
TEST(BoxFeatureTest, ValidateAcceptsPositiveDimensions) {
    BoxFeature box("Box005", 100, 50, 30);
    EXPECT_TRUE(box.validate().empty());      // 返回空串 = 合法
}

// ------------------------------------------------------------
// ④ 校验测试（非法：负数尺寸）
// ------------------------------------------------------------
// 目标：负数尺寸 = 不合法，validate() 必须返回非空错误原因
TEST(BoxFeatureTest, ValidateRejectsNegativeDimension) {
    BoxFeature box("Box006", -5, 50, 30);    // length = -5，非法！
    EXPECT_FALSE(box.validate().empty());     // 返回非空 = 有错误原因
    // 错误原因里应该提到是哪个参数出了问题
    EXPECT_NE(box.validate().find("length"), std::string::npos);
    //   find() 找不到返回 npos；不等于 npos = "错误信息里包含 length"
}

// ------------------------------------------------------------
// ④ 校验测试（非法：零尺寸）
// ------------------------------------------------------------
// 目标：0 也不合法（长宽高必须 > 0，等于 0 是退化形状）
TEST(BoxFeatureTest, ValidateRejectsZeroDimension) {
    BoxFeature box("Box007", 0, 50, 30);     // length = 0，非法！
    EXPECT_FALSE(box.validate().empty());
}

// ------------------------------------------------------------
// ⑤ 重建测试
// ------------------------------------------------------------
// 目标：rebuild() 返回一段描述文本，必须包含 "Box" 和三个尺寸
TEST(BoxFeatureTest, RebuildReturnsDescriptiveText) {
    BoxFeature box("Box008", 100, 50, 30);
    const std::string text = box.rebuild();   // 例如 "Box[id=Box008, length=100.0, ...]"
    EXPECT_NE(text.find("Box"), std::string::npos);     // 包含 "Box"
    EXPECT_NE(text.find("100"), std::string::npos);     // 包含 100
    EXPECT_NE(text.find("50"), std::string::npos);      // 包含 50
    EXPECT_NE(text.find("30"), std::string::npos);      // 包含 30
}
