#pragma once
#include "reconstruction.h"

struct CG : Reconstruction  {
    CLBuffer<mat4> At;
    // Persistent
    real residualEnergy = 0;
    CLVolume r, p;
    // Temporary
    ImageArray Ap;
    CLVolume AtAp;

    ///
    /// \note Releases \a attenuation after backprojection to \a r to ensure enough is left to allocate \a p, \a Ap, \a AtAp
    CG(const Projection& projection, ImageArray&& attenuation);
    void step() override;
};
