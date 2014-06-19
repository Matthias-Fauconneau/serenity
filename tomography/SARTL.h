#pragma once
#include "reconstruction.h"

struct SARTL : SubsetReconstruction {
    buffer<ImageArray> AAti; // A At i
    ImageArray Ax; // At x
    CLVolume Atr; // At r
    CLVolume lnX; // ln x

    SARTL(int3 volumeSize, const ImageArray& b, const uint subsetSize);
    void step() override;
};
