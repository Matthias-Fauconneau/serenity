#pragma once
#include "reconstruction.h"
#include "filter.h"

struct Approximate : Reconstruction  {
    real residualEnergy = 0;
    VolumeF p, r, AtAp;
    Filter filters[coreCount];
    bool filter = true;

    Approximate(uint N, bool filter=false) : Reconstruction(N), p(N), r(N), AtAp(N), filter(filter) {}
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images) override;
    bool step(const ref<Projection>& projections, const ref<ImageF>& images) override;
};
