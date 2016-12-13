#pragma once
#include "math/Vec.h"

struct DirectionSample
{
    Vec3f d;
    Vec3f weight;
    float pdf;

    DirectionSample() = default;
    DirectionSample(const Vec3f &d_)
    : d(d_)
    {
    }
};
