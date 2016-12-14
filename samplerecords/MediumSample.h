#pragma once
#include "math/Vec.h"

struct MediumSample {
    struct PhaseFunction* phase;
    Vec3f p;
    float t;
    Vec3f weight;
    float pdf;
    bool exited;
};
