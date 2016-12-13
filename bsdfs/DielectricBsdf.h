#pragma once
#include "Bsdf.h"

struct Scene;

class DielectricBsdf : public Bsdf
{
    float _ior;
    float _invIor;
    bool _enableT;

public:
    DielectricBsdf();
    DielectricBsdf(float ior);

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;
    virtual rapidjson::Value toJson(Allocator &allocator) const override;

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;
    virtual float eta(const SurfaceScatterEvent &event) const override;

    virtual void prepareForRender() override;


    bool enableTransmission() const
    {
        return _enableT;
    }

    float ior() const
    {
        return _ior;
    }

    void setEnableTransmission(bool enableTransmission)
    {
        _enableT = enableTransmission;
    }

    void setIor(float ior)
    {
        _ior = ior;
    }
};
