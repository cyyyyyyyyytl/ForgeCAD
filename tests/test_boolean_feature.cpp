#include <gtest/gtest.h>

#include "domain/BooleanFeature.h"
#include "geometry/ShapeFactory.h"

#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <stdexcept>

namespace {

double volumeOf(const TopoDS_Shape& shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}

} // namespace

TEST(BooleanFeatureTest, DispatchesDifferenceUnionAndIntersection)
{
    const auto base = forge::geometry::ShapeFactory::makeBox(10.0, 10.0, 10.0);
    ASSERT_FALSE(base.IsNull());
    gp_Trsf placement;
    placement.SetTranslation(gp_Vec(5.0, 5.0, 5.0));
    const auto tool = BRepBuilderAPI_Transform(base, placement).Shape();
    ASSERT_FALSE(tool.IsNull());

    struct Case {
        forge::domain::BooleanOperation operation;
        const char* type;
        double expectedVolume;
    };
    const Case cases[] = {
        {forge::domain::BooleanOperation::Difference, "Cut", 875.0},
        {forge::domain::BooleanOperation::Union, "Union", 1875.0},
        {forge::domain::BooleanOperation::Intersection, "Intersection", 125.0},
    };
    for (const auto& entry : cases) {
        forge::domain::BooleanFeature feature("Result001", entry.operation);
        EXPECT_EQ(feature.id(), "Result001");
        EXPECT_EQ(feature.type(), entry.type);
        EXPECT_TRUE(feature.parameters().empty());
        EXPECT_TRUE(feature.validate().empty());

        const auto result = feature.rebuild({base, tool});
        ASSERT_FALSE(result.IsNull());
        EXPECT_NEAR(volumeOf(result), entry.expectedVolume, 1e-5);
    }
}

TEST(BooleanFeatureTest, RejectsMissingInputsAndNumericParameters)
{
    forge::domain::BooleanFeature feature("Cut001", forge::domain::BooleanOperation::Difference);
    const auto base = forge::geometry::ShapeFactory::makeBox(10.0, 10.0, 10.0);
    ASSERT_FALSE(base.IsNull());

    EXPECT_TRUE(feature.rebuild().IsNull());
    EXPECT_TRUE(feature.rebuild({base}).IsNull());
    EXPECT_TRUE(feature.rebuild({base, TopoDS_Shape{}}).IsNull());
    EXPECT_THROW(feature.setParameter("radius", 2.0), std::invalid_argument);
}
