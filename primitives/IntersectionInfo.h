#pragma once
#include "math/Vec.h"

class Primitive;
class Bsdf;

struct IntersectionInfo
{
    Vec3f Ng;
    Vec3f Ns;
    Vec3f p;
    Vec3f w;
    Vec2f uv;
    float epsilon;

    const Primitive *primitive;
    const Bsdf *bsdf;
};
