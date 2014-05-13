#pragma once
#include "reconstruction.h"
#include "filter.h"

struct Adjoint : Reconstruction  {
    real residualEnergy = 0;
    VolumeF p, r;
    VolumeF AtAp[coreCount];
    Filter filters[coreCount];
    const bool filter, regularize;

    Adjoint(int3 size, uint projectionCount, bool filter = false, bool regularize = false) : Reconstruction(size,projectionCount,"adjoint"_+(filter?"-filter"_:""_)+(regularize?"-regularize"_:""_)), p(size), r(size), filter(filter), regularize(regularize) { for(VolumeF& e: AtAp) e = VolumeF(size); }
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images) override;
    bool step(const ref<Projection>& projections, const ref<ImageF>& images) override;
};
