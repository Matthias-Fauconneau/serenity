#pragma once
#include "reconstruction.h"

struct OSTR : SubsetReconstruction {
    buffer<ImageArray> Ai; // A i
    CLImage b0; // Background noise
    CLImage b1; // Blank scan
    ImageArray Ax; // Projection of current estimate
    ImageArray Aic;
    ImageArray h;
    CLVolume L;
    CLVolume d;

    OSTR(int3 volumeSize, const ImageArray& b);
    void step() override;
};
