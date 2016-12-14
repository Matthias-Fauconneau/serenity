#pragma once
#include "Bsdf.h"

struct Scene;
struct SurfaceScatterEvent;

class ForwardBsdf : public Bsdf
{
public:
    ForwardBsdf();

    virtual bool sample(SurfaceScatterEvent &event) const override;
    virtual Vec3f eval(const SurfaceScatterEvent &event) const override;
    virtual float pdf(const SurfaceScatterEvent &event) const override;
};
