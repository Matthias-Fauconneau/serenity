#pragma once
#include "reconstruction.h"

struct SART : Reconstruction {
    ImageArray Ai; // A i
    CLVolume Ati; // At i
    ImageArray Ax; // Projection of current estimate
    ImageArray h; // b - Ax
    CLVolume L; // At h
    CLVolume oldX;
    CLVolume LoAti; // L / Ati

    SART(int3 volumeSize, const ImageArray& b);
    void step() override;
};
