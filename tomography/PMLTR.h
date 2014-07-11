#pragma once
#include "reconstruction.h"

struct PMLTR : SubsetReconstruction {
    ImageArray Ax; // A x
    ImageArray r;
    CLVolume Atr; // At r
    CLVolume Atw; // At w

    PMLTR(const Projection& projection, ImageArray&& intensity, const uint subsetSize);
    void step() override;
};
