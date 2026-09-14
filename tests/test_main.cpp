#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include "core/Version.h"
#include "geometry/ShapeFactory.h"

// 这一文件覆盖最底层的“冒烟能力”：版本宏是否成功注入，以及 OCCT 是否能
// 创建三种基础实体。更细的参数、文档和 AI 工具测试拆分在其他测试文件中。

// 版本字符串不能为空，也不能退化为未配置时常见的占位值。
TEST(VersionTest, ReturnsVersionString) {
    auto v = forge::core::Version::string();
    EXPECT_FALSE(v.empty());
    EXPECT_NE(v, "0.0.0");
    // 把实际版本写入测试日志，CI 失败时可以确认运行的是哪一次构建。
    spdlog::info("ForgeCAD version = {}", v);
}

// 合法尺寸应生成一个非空的 OCCT TopoDS_Shape。
TEST(ShapeFactoryTest, MakeBoxPositive) {
    auto box = forge::geometry::ShapeFactory::makeBox(10.0, 20.0, 30.0);
    EXPECT_FALSE(box.IsNull());
}

// 非正长度没有几何意义，Factory 应安全地返回空 Shape，而不是让 OCCT 异常越界。
TEST(ShapeFactoryTest, MakeBoxInvalidReturnsNull) {
    auto box = forge::geometry::ShapeFactory::makeBox(0, 20.0, 30.0);
    EXPECT_TRUE(box.IsNull());
}

// 圆柱的半径和高度都有效时，确认 OCCT 构造链路可用。
TEST(ShapeFactoryTest, MakeCylinderPositive) {
    auto cyl = forge::geometry::ShapeFactory::makeCylinder(20.0, 60.0);
    EXPECT_FALSE(cyl.IsNull());
}

// 半径为零时拒绝创建退化圆柱。
TEST(ShapeFactoryTest, MakeCylinderInvalidReturnsNull) {
    auto cyl = forge::geometry::ShapeFactory::makeCylinder(0, 60.0);
    EXPECT_TRUE(cyl.IsNull());
}

// 合法半径应创建非空球体。
TEST(ShapeFactoryTest, MakeSpherePositive) {
    auto sph = forge::geometry::ShapeFactory::makeSphere(20.0);
    EXPECT_FALSE(sph.IsNull());
}

// 半径为零时拒绝创建退化球体。
TEST(ShapeFactoryTest, MakeSphereInvalidReturnsNull) {
    auto sph = forge::geometry::ShapeFactory::makeSphere(0);
    EXPECT_TRUE(sph.IsNull());
}
