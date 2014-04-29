#pragma once
#include "reconstruction.h"
#include "filter.h"

struct Adjoint : Reconstruction  {
    real residualEnergy = 0;
    VolumeF p, r;
    VolumeF AtAp[coreCount];
    Filter filters[coreCount];
    bool filter = true;

    Adjoint(uint N, bool filter = false) : Reconstruction(N), p(N), r(N), filter(filter) { for(VolumeF& e: AtAp) e = VolumeF(N); }
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images) override;
    bool step(const ref<Projection>& projections, const ref<ImageF>& images) override;
};
