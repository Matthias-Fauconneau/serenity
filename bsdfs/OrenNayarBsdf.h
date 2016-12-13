#pragma once
#include "Bsdf.h"

struct Scene;

class OrenNayarBsdf : public Bsdf
{
    std::shared_ptr<Texture> _roughness;

public:
    OrenNayarBsdf();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;
    virtual rapidjson::Value toJson(Allocator &allocator) const override;

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;

    const std::shared_ptr<Texture> &roughness() const
    {
        return _roughness;
    }

    void setRoughness(const std::shared_ptr<Texture> &roughness)
    {
        _roughness = roughness;
    }
};
