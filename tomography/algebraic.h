#pragma once
#include "reconstruction.h"

struct Algebraic : Reconstruction {
    ImageArray AAti; // At i
    ImageArray Ax; // Projection of current estimate
    CLVolume p; // At ( b - Ax )

    Algebraic(int3 volumeSize, const ImageArray& b);
    void step() override;
};
