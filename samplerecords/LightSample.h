#pragma once
#include "math/Vec.h"

class Medium;

struct LightSample
{
    Vec3f d;
    float dist;
    float pdf;
    const Medium *medium;
};
