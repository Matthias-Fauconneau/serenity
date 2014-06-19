#pragma once
#include "reconstruction.h"

// MLEM <=> MART <=> SART o log
struct MART : SubsetReconstruction {
    buffer<ImageArray> AAti; // A At i
    ImageArray Ax; // At x
    CLVolume Atr; // At r

    MART(int3 volumeSize, const ImageArray& b, const uint subsetSize);
    void step() override;
};
