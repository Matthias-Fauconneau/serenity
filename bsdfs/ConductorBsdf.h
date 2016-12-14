#pragma once
#include "Bsdf.h"

struct Scene;

class ConductorBsdf : public Bsdf
{
    std::string _materialName;
    Vec3f _eta;
    Vec3f _k;

    void lookupMaterial();

public:
    ConductorBsdf();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;

    Vec3f eta() const
    {
        return _eta;
    }

    Vec3f k() const
    {
        return _k;
    }

    const std::string &materialName() const
    {
        return _materialName;
    }

    void setEta(Vec3f eta)
    {
        _eta = eta;
    }

    void setK(Vec3f k)
    {
        _k = k;
    }

    void setMaterialName(const std::string &materialName)
    {
        _materialName = materialName;
        lookupMaterial();
    }
};
