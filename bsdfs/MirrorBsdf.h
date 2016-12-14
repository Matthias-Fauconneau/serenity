#pragma once
#include "Bsdf.h"

struct Scene;

class MirrorBsdf : public Bsdf
{
public:
    MirrorBsdf();

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;
};
