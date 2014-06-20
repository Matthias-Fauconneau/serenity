#pragma once
#include "reconstruction.h"

struct MART : SubsetReconstruction {
    buffer<ImageArray> AAti; // A At i
    ImageArray Ax; // At x
    CLVolume Atr; // At r

    MART(const Projection& projection, const ImageArray& b,const uint subsetSize);
    void step() override;
};
