#pragma once
#include "reconstruction.h"

struct MLTR : SubsetReconstruction {
    array<ImageArray> Ai; // A i
    ImageArray Ax; // A x
    ImageArray r;
    CLVolume Atr; // At r
    CLVolume Atw; // At w

    MLTR(const Projection& projection, ImageArray&& intensity, const uint subsetSize);
    void step() override;
};
