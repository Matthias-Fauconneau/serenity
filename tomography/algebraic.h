#pragma once
#include "reconstruction.h"

struct Algebraic : Reconstruction {
    buffer<ImageArray> AAti; // A At i
    ImageArray Ax; // Projection of current estimate
    CLVolume p; // At ( b - Ax )

    Algebraic(int3 volumeSize, const ImageArray& b);
    void step() override;
};
