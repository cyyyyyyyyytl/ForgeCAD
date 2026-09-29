#pragma once

#include "domain/Feature.h"

namespace forge::domain {

    enum class RevolveAxis { X = 0, Y = 1, Z = 2 };

    class RevolveFeature final : public Feature {
    public:
        RevolveFeature(
            std::string id,
            double angle,
            RevolveAxis axis = RevolveAxis::Y,
            double x = 0, double y = 0, double z = 0);

        const std::vector<Parameter>& parameters() const override;

        void setParameter(
            const std::string& name,
            ParameterValue value) override;

        std::string validate() const override;

        TopoDS_Shape rebuild(
            const std::vector<TopoDS_Shape>& inputs = {}) const override;
        core::ShapeResult rebuildResult(
            const std::vector<TopoDS_Shape>& inputs = {}) const override;

    private:
        std::vector<Parameter> parameters_;
    };

} // namespace forge::domain
