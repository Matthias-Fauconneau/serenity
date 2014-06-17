#pragma once
#include "reconstruction.h"

struct Algebraic : SubsetReconstruction {
    buffer<ImageArray> AAti; // A At i
    ImageArray Ax; // At x
    CLVolume Atr; // At r

    Algebraic(int3 volumeSize, const ImageArray& b);
    void step() override;
};
