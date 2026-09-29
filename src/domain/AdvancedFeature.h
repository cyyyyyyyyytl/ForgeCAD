#pragma once
#include "domain/Feature.h"
#include "domain/FeatureRegistry.h"
#include "domain/FeatureDefinition.h"

namespace forge::domain {
class AdvancedFeature final : public Feature {
public:
    AdvancedFeature(std::string id,std::string type,const NumericParameters& parameters,
                    FeatureDefinition definition={});
    static bool isType(std::string_view type);
    static bool consumesInputs(std::string_view type);
    const std::vector<Parameter>& parameters() const override { return parameters_; }
    void setParameter(const std::string& name,ParameterValue value) override;
    std::string validate() const override;
    TopoDS_Shape rebuild(const std::vector<TopoDS_Shape>& inputs={}) const override;
    core::ShapeResult rebuildResult(const std::vector<TopoDS_Shape>& inputs={}) const override;
    const FeatureDefinition& definition() const { return definition_; }
    void setDefinition(FeatureDefinition definition) { definition_=std::move(definition); }
private:
    double value(const char* name) const;
    std::vector<Parameter> parameters_;
    FeatureDefinition definition_;
};
}
