#include <gtest/gtest.h>

#include "geometry/ShapeFactory.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace {

double volumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

} // namespace

TEST(ShapeFactoryCutTest, RemovesOverlappingVolume)
{
    const auto box = forge::geometry::ShapeFactory::makeBox(10.0, 10.0, 10.0);
    const auto cylinder = forge::geometry::ShapeFactory::makeCylinder(2.0, 20.0);
    ASSERT_FALSE(box.IsNull());
    ASSERT_FALSE(cylinder.IsNull());

    // Put the cutter through the box interior. Its extra height avoids
    // coincident top and bottom faces during the Boolean operation.
    gp_Trsf placement;
    placement.SetTranslation(gp_Vec(5.0, 5.0, -5.0));
    const auto tool = BRepBuilderAPI_Transform(cylinder, placement).Shape();

    const auto result = forge::geometry::ShapeFactory::booleanDifference(box, tool);
    ASSERT_FALSE(result.IsNull());
    EXPECT_LT(volumeOf(result), volumeOf(box));
    EXPECT_NEAR(volumeOf(result), 1000.0 - 40.0 * 3.14159265358979323846, 1e-5);
}

TEST(ShapeFactoryCutTest, RejectsNullInput)
{
    const auto box = forge::geometry::ShapeFactory::makeBox(10.0, 10.0, 10.0);
    ASSERT_FALSE(box.IsNull());

    EXPECT_TRUE(forge::geometry::ShapeFactory::booleanDifference(TopoDS_Shape{}, box).IsNull());
    EXPECT_TRUE(forge::geometry::ShapeFactory::booleanDifference(box, TopoDS_Shape{}).IsNull());
}

TEST(ShapeFactoryBooleanTest, UnionAndIntersectionHaveExpectedVolumes)
{
    const auto first = forge::geometry::ShapeFactory::makeBox(10.0, 10.0, 10.0);
    ASSERT_FALSE(first.IsNull());

    gp_Trsf placement;
    placement.SetTranslation(gp_Vec(5.0, 5.0, 5.0));
    const auto second = BRepBuilderAPI_Transform(first, placement).Shape();
    ASSERT_FALSE(second.IsNull());

    // Each cube has volume 1000; their shared 5x5x5 region has volume 125.
    const auto joined = forge::geometry::ShapeFactory::booleanUnion(first, second);
    ASSERT_FALSE(joined.IsNull());
    EXPECT_NEAR(volumeOf(joined), 1875.0, 1e-5);

    const auto shared = forge::geometry::ShapeFactory::booleanIntersection(first, second);
    ASSERT_FALSE(shared.IsNull());
    EXPECT_NEAR(volumeOf(shared), 125.0, 1e-5);
}

TEST(ShapeFactoryBooleanTest, UnionAndIntersectionRejectNullInput)
{
    const auto box = forge::geometry::ShapeFactory::makeBox(10.0, 10.0, 10.0);
    ASSERT_FALSE(box.IsNull());

    EXPECT_TRUE(forge::geometry::ShapeFactory::booleanUnion(TopoDS_Shape{}, box).IsNull());
    EXPECT_TRUE(forge::geometry::ShapeFactory::booleanUnion(box, TopoDS_Shape{}).IsNull());
    EXPECT_TRUE(forge::geometry::ShapeFactory::booleanIntersection(TopoDS_Shape{}, box).IsNull());
    EXPECT_TRUE(forge::geometry::ShapeFactory::booleanIntersection(box, TopoDS_Shape{}).IsNull());
}
