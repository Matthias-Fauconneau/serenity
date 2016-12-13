#pragma once
#include "math/Vec.h"

class PhaseFunction;

struct MediumSample
{
    PhaseFunction *phase;
    Vec3f p;
    float t;
    Vec3f weight;
    float pdf;
    bool exited;
};
