#pragma once
#include "Bsdf.h"

struct Scene;

class PlasticBsdf : public Bsdf
{
    float _ior;
    float _thickness;
    Vec3f _sigmaA;

    float _diffuseFresnel;
    float _avgTransmittance;
    Vec3f _scaledSigmaA;

public:
    PlasticBsdf();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;

    virtual void prepareForRender() override;

    float ior() const
    {
        return _ior;
    }

    float thickness() const
    {
        return _thickness;
    }

    Vec3f sigmaA() const
    {
        return _sigmaA;
    }

    void setIor(float ior)
    {
        _ior = ior;
    }

    void setSigmaA(Vec3f sigmaA)
    {
        _sigmaA = sigmaA;
    }

    void setThickness(float thickness)
    {
        _thickness = thickness;
    }
};
