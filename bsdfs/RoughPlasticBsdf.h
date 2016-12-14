#pragma once
#include "Bsdf.h"
#include "Microfacet.h"

struct Scene;

class RoughPlasticBsdf : public Bsdf
{
    float _ior;
    float _thickness;
    Vec3f _sigmaA;
    std::string _distributionName;
    std::shared_ptr<Texture> _roughness;

    float _diffuseFresnel;
    float _avgTransmittance;
    Vec3f _scaledSigmaA;
    Microfacet::Distribution _distribution;

public:
    RoughPlasticBsdf();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;

    virtual void prepareForRender() override;

    const std::string &distributionName() const
    {
        return _distributionName;
    }

    float ior() const
    {
        return _ior;
    }

    const std::shared_ptr<Texture> &roughness() const
    {
        return _roughness;
    }

    Vec3f sigmaA() const
    {
        return _sigmaA;
    }

    float thickness() const
    {
        return _thickness;
    }

    void setDistributionName(const std::string &distributionName)
    {
        _distributionName = distributionName;
    }

    void setIor(float ior)
    {
        _ior = ior;
    }

    void setRoughness(const std::shared_ptr<Texture> &roughness)
    {
        _roughness = roughness;
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
