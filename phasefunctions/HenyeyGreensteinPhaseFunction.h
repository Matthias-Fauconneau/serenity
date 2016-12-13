#pragma once
#include "PhaseFunction.h"

class HenyeyGreensteinPhaseFunction : public PhaseFunction
{
    float _g;

    inline float henyeyGreenstein(float cosTheta) const;

public:
    HenyeyGreensteinPhaseFunction();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;
    virtual rapidjson::Value toJson(Allocator &allocator) const override;

    virtual Vec3f eval(const Vec3f &wi, const Vec3f &wo) const override;
    virtual bool sample(PathSampleGenerator &sampler, const Vec3f &wi, PhaseSample &sample) const override;
    virtual float pdf(const Vec3f &wi, const Vec3f &wo) const override;

    float g() const
    {
        return _g;
    }
};
