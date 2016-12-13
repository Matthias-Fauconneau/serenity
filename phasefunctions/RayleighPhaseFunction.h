#pragma once
#include "PhaseFunction.h"

class RayleighPhaseFunction : public PhaseFunction
{
    static inline float rayleigh(float cosTheta);
public:
    virtual rapidjson::Value toJson(Allocator &allocator) const override;

    virtual Vec3f eval(const Vec3f &wi, const Vec3f &wo) const override;
    virtual bool sample(PathSampleGenerator &sampler, const Vec3f &wi, PhaseSample &sample) const override;
    virtual float pdf(const Vec3f &wi, const Vec3f &wo) const override;
};
