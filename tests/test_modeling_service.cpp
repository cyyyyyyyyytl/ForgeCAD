#include <gtest/gtest.h>

#include "application/ModelDocument.h"
#include "application/ModelingService.h"
#include "domain/Feature.h"

using forge::application::ModelDocument;
using forge::application::ModelingService;
using forge::domain::NumericParameters;

TEST(ModelingServiceTest, CreatesFeatureThroughNamedApplicationInterface)
{
    ModelDocument document;
    ModelingService service(document);

    auto& feature = service.createFeature("Cylinder", {
        {"height", 60.0},
        {"radius", 20.0},
    });

    EXPECT_EQ(feature.id(), "Cylinder001");
    EXPECT_EQ(document.findFeature("Cylinder001"), &feature);
    EXPECT_FALSE(feature.rebuild().IsNull());
}

TEST(ModelingServiceTest, ModifiesParameterByStableFeatureId)
{
    ModelDocument document;
    ModelingService service(document);
    auto& feature = service.createFeature("Sphere", {{"radius", 20.0}});

    service.setParameter(feature.id(), "radius", 35.0);

    EXPECT_DOUBLE_EQ(feature.parameters()[0].asDouble(), 35.0);
}

TEST(ModelingServiceTest, RejectsInvalidFeatureParameterOrId)
{
    ModelDocument document;
    ModelingService service(document);
    auto& feature = service.createFeature("Sphere", {{"radius", 20.0}});

    EXPECT_THROW(service.setParameter(feature.id(), "radius", -5.0),
                 std::invalid_argument);
    EXPECT_DOUBLE_EQ(feature.parameters()[0].asDouble(), 20.0);

    EXPECT_THROW(service.setParameter(feature.id(), "height", 10.0),
                 std::invalid_argument);
    EXPECT_THROW(service.setParameter("missing", "radius", 10.0),
                 std::invalid_argument);
}
