#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include "core/Version.h"
#include "geometry/ShapeFactory.h"

// 验证版本号
TEST(VersionTest, ReturnsVersionString) {
    auto v = forge::core::Version::string();
    EXPECT_FALSE(v.empty());
    EXPECT_NE(v, "0.0.0");
    spdlog::info("ForgeCAD version = {}", v);
}

// 验证 OCCT Box 创建
TEST(ShapeFactoryTest, MakeBoxPositive) {
    auto box = forge::geometry::ShapeFactory::makeBox(10.0, 20.0, 30.0);
    EXPECT_FALSE(box.IsNull());
}

TEST(ShapeFactoryTest, MakeBoxInvalidReturnsNull) {
    auto box = forge::geometry::ShapeFactory::makeBox(0, 20.0, 30.0);
    EXPECT_TRUE(box.IsNull());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
