#pragma once
#include "reconstruction.h"

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations)
struct CG : Reconstruction  {
    CLBuffer<mat4> At;
    real residualEnergy = 0;
    CLVolume r;
    CLVolume p;
    ImageArray Ap; // A p
    CLVolume AtAp; // At A p

    CG(const Projection& projection, ImageArray&& attenuation);
    void step() override;
};
