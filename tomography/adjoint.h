#pragma once
#include "reconstruction.h"

struct Adjoint : Reconstruction  {
    real residualEnergy = 0;
    VolumeF p, r, AtAp[coreCount];

    Adjoint(uint N) : Reconstruction(N), p(N), r(N) { for(VolumeF& e: AtAp) e = VolumeF(N); }
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images) override;
    bool step(const ref<Projection>& projections, const ref<ImageF>& images) override;
};
