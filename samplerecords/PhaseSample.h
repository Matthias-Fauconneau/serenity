#pragma once
#include "math/Vec.h"

struct PhaseSample
{
    Vec3f w;
    Vec3f weight;
    float pdf;
};
