#pragma once

#include "domain/Feature.h"

namespace forge::domain {

    class RectangleSketchFeature final : public Feature {
    public:
        RectangleSketchFeature(
            std::string id,
            double length,
            double width,
            double x = 0.0,
            double y = 0.0,
            double z = 0.0);

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
