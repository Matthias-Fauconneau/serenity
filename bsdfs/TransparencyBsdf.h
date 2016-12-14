#pragma once
#include "Bsdf.h"

class TransparencyBsdf : public Bsdf
{
    std::shared_ptr<Texture> _opacity;
    std::shared_ptr<Bsdf> _base;

public:
    TransparencyBsdf();
    TransparencyBsdf(std::shared_ptr<Texture> opacity, std::shared_ptr<Bsdf> base);

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;

    virtual void prepareForRender() override;

    const std::shared_ptr<Texture> &opacity() const
    {
        return _opacity;
    }

    const std::shared_ptr<Bsdf> &base() const
    {
        return _base;
    }

    void setOpacity(const std::shared_ptr<Texture> &opacity)
    {
        _opacity = opacity;
    }

    void setBase(const std::shared_ptr<Bsdf> &base)
    {
        _base = base;
    }
};
