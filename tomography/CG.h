#pragma once
#include "reconstruction.h"

struct CG : Reconstruction  {
    CLBuffer<mat4> At;
    // Persistent
    real residualEnergy = 0;
    CLVolume p, r;
    // Temporary
    const ImageArray Ap;
    const CLVolume AtAp;

    CG(const Projection& projection, const ImageArray& b);
    void step() override;
};
