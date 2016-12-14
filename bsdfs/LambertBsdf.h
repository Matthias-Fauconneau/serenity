#pragma once
#include "Bsdf.h"

struct Scene;
struct SurfaceScatterEvent;

class LambertBsdf : public Bsdf
{
public:
    LambertBsdf();

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;
};
