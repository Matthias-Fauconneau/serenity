#pragma once
#include "reconstruction.h"

struct SART : SubsetReconstruction {
    array<ImageArray> AAti; // A At i
    ImageArray Ax; // At x
    CLVolume Atr; // At r

    SART(const Projection& A, ImageArray&& attenuation, const uint subsetSize);
    void step() override;
};
