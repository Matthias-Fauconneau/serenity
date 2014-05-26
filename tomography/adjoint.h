#pragma once
#include "reconstruction.h"
#include "filter.h"

struct Adjoint : Reconstruction  {
    real residualEnergy = 0;
    VolumeF p, r;
    VolumeF AtAp[coreCount];
    Filter filters[coreCount];
    const bool filter, regularize;

    Adjoint(int3 reconstructionSize, int3 projectionSize, bool filter = false, bool regularize = false, string label=""_) : Reconstruction(reconstructionSize,projectionSize,label+"adjoint"_+(filter?"-filter"_:""_)+(regularize?"-regularize"_:""_)), p(reconstructionSize), r(reconstructionSize), filter(filter), regularize(regularize) { for(VolumeF& e: AtAp) e = VolumeF(reconstructionSize); }
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images) override;
    bool step(const ref<Projection>& projections, const ref<ImageF>& images) override;
};
