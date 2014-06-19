#pragma once
#include "reconstruction.h"

struct MLTR : SubsetReconstruction {
    buffer<ImageArray> Ai; // A i
    ImageArray Ax; // A x
    ImageArray r;
    CLVolume Atr; // At r
    CLVolume Atw; // At w

    MLTR(int3 volumeSize, const ImageArray& b, const uint subsetSize);
    void step() override;
};
