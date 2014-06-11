#pragma once
#include "reconstruction.h"

struct OSTR : Reconstruction {
    Random random[coreCount];
    OSTR(int3 volumeSize, const ImageArray& b);
    void step() override;
};
