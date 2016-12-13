#pragma once
#include "math/Vec.h"

struct LensSample
{
    Vec2f pixel;
    Vec3f d;
    float dist;
    Vec3f weight;
};
