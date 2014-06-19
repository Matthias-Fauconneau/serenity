#pragma once
#include "reconstruction.h"

struct CG : Reconstruction  {
    ProjectionArray At;
    // Persistent
    real residualEnergy = 0;
    CLVolume p, r;
    // Temporary
    const ImageArray Ap;
    const CLVolume AtAp;

    CG(int3 volumeSize, const ImageArray& images);
    void step() override;
};
