#pragma once
#include "reconstruction.h"

struct PMLTR : SubsetReconstruction {
    ImageArray Ax; // A x
    ImageArray r;
    CLVolume Atr; // At r
    CLVolume Atw; // At w

    PMLTR(int3 volumeSize, const ImageArray& b, const uint subsetSize);
    void step() override;
};
