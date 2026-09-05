// ============================================================
// FeatureFactory 单元测试
// ------------------------------------------------------------
// 测什么：
//   ① 按名字造出正确的具体类（Box → name()=="Box"）
//   ② 参数个数不对 → 抛异常（防止越界）
//   ③ 未知类型 → 抛异常
//   ④ 多态测试（最有价值）：基类容器装两种具体类，
//      同一个循环统一操作——证明"加新特征，系统代码不改"
// ============================================================
#include <gtest/gtest.h>                // gtest
#include <stdexcept>                    // std::invalid_argument
#include <vector>                       // std::vector
#include <memory>                       // std::unique_ptr
#include "domain/FeatureFactory.h"      // 被测对象：工厂
#include "domain/Feature.h"             // 基类接口（多态测试用）

using namespace forge::domain;

// ① Box：工厂按名字造出 Box，参数 3 个，重建出真形状
TEST(FeatureFactoryTest, CreateBoxReturnsBox) {
    auto f = FeatureFactory::create("Box", "B1", {100.0, 50.0, 30.0});
    EXPECT_EQ(f->name(), "Box");
    EXPECT_EQ(f->id(), "B1");
    EXPECT_EQ(f->parameters().size(), 3u);
    EXPECT_FALSE(f->rebuild().IsNull());
}

// ① Cylinder：工厂按名字造出 Cylinder，参数 2 个
TEST(FeatureFactoryTest, CreateCylinderReturnsCylinder) {
    auto f = FeatureFactory::create("Cylinder", "C1", {20.0, 60.0});
    EXPECT_EQ(f->name(), "Cylinder");
    EXPECT_EQ(f->id(), "C1");
    EXPECT_EQ(f->parameters().size(), 2u);
    EXPECT_FALSE(f->rebuild().IsNull());
}

// ② 参数个数不对：Box 只给 2 个数 → 抛异常（而不是数组越界）
TEST(FeatureFactoryTest, WrongArgCountThrows) {
    EXPECT_THROW(FeatureFactory::create("Box", "B1", {1.0, 2.0}), std::invalid_argument);
    EXPECT_THROW(FeatureFactory::create("Cylinder", "C1", {1.0}), std::invalid_argument);
}

// ③ 未知类型 → 抛异常（注意：不能拿已支持的类型举例——
//    曾用 "Sphere" 当未知类型，球体上线后此测试就红了：测试假设会过时）
TEST(FeatureFactoryTest, UnknownTypeThrows) {
    EXPECT_THROW(FeatureFactory::create("Torus", "T1", {1.0}), std::invalid_argument);
}

// ① Sphere：工厂按名字造出球体，参数 1 个
TEST(FeatureFactoryTest, CreateSphereReturnsSphere) {
    auto f = FeatureFactory::create("Sphere", "S1", {20.0});
    EXPECT_EQ(f->name(), "Sphere");
    EXPECT_EQ(f->id(), "S1");
    EXPECT_EQ(f->parameters().size(), 1u);
    EXPECT_FALSE(f->rebuild().IsNull());
}

// ② 参数个数不对：Sphere 只给 2 个数 → 抛异常（而不是数组越界）
TEST(FeatureFactoryTest, CreateSphereWrongArgCountThrows) {
    EXPECT_THROW(FeatureFactory::create("Sphere", "S1", {1.0, 2.0}), std::invalid_argument);
}

// ④ 多态测试：基类容器装 Box + Cylinder + Sphere，同一个循环统一驱动
//    ——系统只认识 Feature，不认识具体类，也能让每个对象干自己的活
TEST(FeatureFactoryTest, UniformOperationsOnMixedFeatures) {
    std::vector<std::unique_ptr<Feature>> parts;   // 基类容器
    parts.push_back(FeatureFactory::create("Box", "Box001", {100.0, 50.0, 30.0}));
    parts.push_back(FeatureFactory::create("Cylinder", "Cyl001", {20.0, 60.0}));
    parts.push_back(FeatureFactory::create("Sphere", "Sph001", {20.0}));

    for (auto& f : parts) {                        // 同一个循环驱动三种特征
        EXPECT_TRUE(f->validate().empty());        // 各自校验自己
        EXPECT_FALSE(f->rebuild().IsNull());       // 各自重建出真形状
    }
    EXPECT_EQ(parts[0]->name(), "Box");            // 各自报自己的名字
    EXPECT_EQ(parts[1]->name(), "Cylinder");
    EXPECT_EQ(parts[2]->name(), "Sphere");
}
