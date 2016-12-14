#pragma once
#include "Bsdf.h"

struct Scene;

class ThinSheetBsdf : public Bsdf
{
    float _ior;
    bool _enableInterference;
    std::shared_ptr<Texture> _thickness;
    Vec3f _sigmaA;

public:
    ThinSheetBsdf();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;

    float ior() const
    {
        return _ior;
    }

    bool enableInterference() const
    {
        return _enableInterference;
    }

    const std::shared_ptr<Texture> &thickness() const
    {
        return _thickness;
    }

    Vec3f sigmaA() const
    {
        return _sigmaA;
    }

    void setEnableInterference(bool enableInterference)
    {
        _enableInterference = enableInterference;
    }

    void setIor(float ior)
    {
        _ior = ior;
    }

    void setThickness(const std::shared_ptr<Texture> &thickness)
    {
        _thickness = thickness;
    }

    void setSigmaA(Vec3f sigmaA)
    {
        _sigmaA = sigmaA;
    }
};
