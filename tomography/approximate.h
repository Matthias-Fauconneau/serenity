#pragma once
#include "reconstruction.h"

struct Approximate : Reconstruction  {
    real residualEnergy = 0;
    VolumeF p, r, AtAp;

    Approximate(uint N) : Reconstruction(N), p(N), r(N), AtAp(N) {}
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images) override;
    bool step(const ref<Projection>& projections, const ref<ImageF>& images) override;
};
