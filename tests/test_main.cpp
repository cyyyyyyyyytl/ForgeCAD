#include <gtest/gtest.h>          // GoogleTest 测试定义和断言宏。
#include <spdlog/spdlog.h>        // 在测试日志中输出实际构建版本。
#include "core/Version.h"        // 验证 CMake 版本宏注入结果。
#include "geometry/ShapeFactory.h" // 验证三种 OCCT 基础体构造链路。

// 这一文件覆盖最底层的“冒烟能力”：版本宏是否成功注入，以及 OCCT 是否能
// 创建三种基础实体。更细的参数、文档和 AI 工具测试拆分在其他测试文件中。

// 版本字符串不能为空，也不能退化为未配置时常见的占位值。
TEST(VersionTest, ReturnsVersionString) {
    auto v = forge::core::Version::string(); // 读取当前二进制编译时的版本字符串。
    EXPECT_FALSE(v.empty());                 // 版本不能是空字符串。
    EXPECT_NE(v, "0.0.0");                 // 不能退化为未配置占位版本。
    // 把实际版本写入测试日志，CI 失败时可以确认运行的是哪一次构建。
    spdlog::info("ForgeCAD version = {}", v);
}

// 合法尺寸应生成一个非空的 OCCT TopoDS_Shape。
TEST(ShapeFactoryTest, MakeBoxPositive) {
    auto box = forge::geometry::ShapeFactory::makeBox(10.0, 20.0, 30.0); // 合法长宽高。
    EXPECT_FALSE(box.IsNull()); // 非空表示 OCCT 构造成功。
}

// 非正长度没有几何意义，Factory 应安全地返回空 Shape，而不是让 OCCT 异常越界。
TEST(ShapeFactoryTest, MakeBoxInvalidReturnsNull) {
    auto box = forge::geometry::ShapeFactory::makeBox(0, 20.0, 30.0); // 长度退化。
    EXPECT_TRUE(box.IsNull()); // Factory 失败协议是返回空 Shape。
}

// 圆柱的半径和高度都有效时，确认 OCCT 构造链路可用。
TEST(ShapeFactoryTest, MakeCylinderPositive) {
    auto cyl = forge::geometry::ShapeFactory::makeCylinder(20.0, 60.0); // 合法半径高度。
    EXPECT_FALSE(cyl.IsNull()); // 应得到非空圆柱体。
}

// 半径为零时拒绝创建退化圆柱。
TEST(ShapeFactoryTest, MakeCylinderInvalidReturnsNull) {
    auto cyl = forge::geometry::ShapeFactory::makeCylinder(0, 60.0); // 半径为零。
    EXPECT_TRUE(cyl.IsNull()); // 退化圆柱应被拒绝。
}

// 合法半径应创建非空球体。
TEST(ShapeFactoryTest, MakeSpherePositive) {
    auto sph = forge::geometry::ShapeFactory::makeSphere(20.0); // 合法正半径。
    EXPECT_FALSE(sph.IsNull()); // 应得到非空球体。
}

// 半径为零时拒绝创建退化球体。
TEST(ShapeFactoryTest, MakeSphereInvalidReturnsNull) {
    auto sph = forge::geometry::ShapeFactory::makeSphere(0); // 半径为零会退化。
    EXPECT_TRUE(sph.IsNull()); // Factory 应返回空 Shape。
}
