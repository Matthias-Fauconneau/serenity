#pragma once
#include "reconstruction.h"

struct ConjugateGradient : Reconstruction  {
    ProjectionArray At;
    // Persistent
    real residualEnergy = 0;
    const CLVolume p, r;
    // Temporary
    const ImageArray Ap;
    const CLVolume AtAp;

    ConjugateGradient(int3 volumeSize, const ImageArray& images);
    void step() override;
};
