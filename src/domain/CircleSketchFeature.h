#pragma once

#include "domain/Feature.h"

namespace forge::domain {

    class CircleSketchFeature final : public Feature {
    public:
        CircleSketchFeature (
            std::string id,
            double radius,
            double x = 0.0,
            double y = 0.0,
            double z = 0.0, int plane = 0);

        const std::vector<Parameter>& parameters() const override;

        void setParameter(
            const std::string& name,
            ParameterValue value) override;

        std::string validate() const override;

        TopoDS_Shape rebuild(
            const std::vector<TopoDS_Shape>& inputs = {}) const override;

    private:
        std::vector<Parameter> parameters_;
    };

} // namespace forge::domain
