#pragma once
#include "reconstruction.h"

struct MLEM : Reconstruction {
    Random random[coreCount];
    MLEM(uint N) : Reconstruction(N) {}
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images) override;
    bool step(const ref<Projection>& projections, const ref<ImageF>& images) override;
};
