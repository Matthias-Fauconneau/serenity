#pragma once
#include "reconstruction.h"

struct ConjugateGradient : Reconstruction  {
    // Persistent
    real residualEnergy = 0;
    const VolumeF p, r;
    // Temporary
    const ImageArray Ap;
    const VolumeF AtAp;
    // DEPRECATED
    const ImageArray AAti;

    ConjugateGradient(int3 volumeSize, const ImageArray& images);
    bool step() override;
};
