#pragma once

#include "domain/Feature.h"

namespace forge::domain {

    enum class ExtrudeDirection { Forward = 0, Reverse = 1, Symmetric = 2 };

    class ExtrudeFeature final : public Feature {
    public:
        ExtrudeFeature(std::string id, double height,
            ExtrudeDirection direction = ExtrudeDirection::Forward);

        const std::vector<Parameter>& parameters() const override;

        void setParameter(const std::string& name,ParameterValue value) override;

        std::string validate() const override;

        TopoDS_Shape rebuild(const std::vector<TopoDS_Shape>& inputs = {}) const override;

    private:
        std::vector<Parameter> parameters_;
    };

} // namespace forge::domain
