#pragma once
#include "reconstruction.h"

// MLEM <=> MART <=> SART o log
struct MLEM : SubsetReconstruction {
    buffer<CLVolume> Ati; // At i
    ImageArray Ax; // At x
    CLVolume Atr; // At r

    MLEM(const Projection& projection, const ImageArray& b, const uint subsetSize);
    void step() override;
};
