#pragma once
#include "reconstruction.h"

struct MLTR : SubsetReconstruction {
    buffer<ImageArray> Ai; // A i
    ImageArray Ax; // A x
    ImageArray r;
    CLVolume Atr; // At r
    CLVolume Atw; // At r

    MLTR(int3 volumeSize, const ImageArray& b);
    void step() override;
};
