#pragma once
#include "reconstruction.h"

struct SART : SubsetReconstruction {
    array<ImageArray> AAti; // A At i
    ImageArray Ax; // At x
    CLVolume Atr; // At r

    SART(const Projection& A, const ImageArray& b, const uint subsetSize);
    void step() override;
};
