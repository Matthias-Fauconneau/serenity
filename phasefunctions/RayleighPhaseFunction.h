#pragma once
#include "PhaseFunction.h"

struct RayleighPhaseFunction : public PhaseFunction {
    static inline float rayleigh(float cosTheta);

    virtual Vec3f eval(const Vec3f &wi, const Vec3f &wo) const override;
    virtual bool sample(PathSampleGenerator &sampler, const Vec3f &wi, PhaseSample &sample) const override;
    virtual float pdf(const Vec3f &wi, const Vec3f &wo) const override;
};
