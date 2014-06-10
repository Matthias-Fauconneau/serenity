#pragma once
#include "reconstruction.h"

struct ConjugateGradient : Reconstruction  {
    // Persistent
    real residualEnergy = 0;
    const VolumeF p, r;
    // Temporary
    const ImageArray Ap;
    const VolumeF AtAp;

    ConjugateGradient(int3 volumeSize, const ImageArray& images);
    void step() override;
};
