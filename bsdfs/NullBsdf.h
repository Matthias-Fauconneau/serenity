#pragma once
#include "Bsdf.h"

struct Scene;
struct SurfaceScatterEvent;

class NullBsdf : public Bsdf
{
public:
    NullBsdf();

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;
};
