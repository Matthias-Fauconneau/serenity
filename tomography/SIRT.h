#pragma once
#include "reconstruction.h"

struct SIRT : Reconstruction {
    Random random[coreCount];
    SIRT(int3 N) : Reconstruction(N) {}
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images) override;
    bool step(const ref<Projection>& projections, const ref<ImageF>& images) override;
};
