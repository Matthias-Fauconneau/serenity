#pragma once
#include "reconstruction.h"

struct MLTR : SubsetReconstruction {
    buffer<ImageArray> Ai; // A i
    ImageArray Ax; // A x
    ImageArray r; // DEBUG
    CLVolume Atr; // At r
    ImageArray w; // DEBUG
    CLVolume Atw; // At r
    CLVolume AtrAtw; // At r / At w

    MLTR(int3 volumeSize, const ImageArray& b);
    void step() override;
};
