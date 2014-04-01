#pragma once
#include "matrix.h"
#include "volume.h"

struct Ellipsoid {
    mat3 M;
    vec3 center;
    float value;
    Ellipsoid(float value, vec3 scale, vec3 center, vec3 angles);
    bool contains(vec3 point) const;
    float intersect(vec3 point, vec3 direction) const;
};

struct Phantom {
    Phantom();
    VolumeF volume(int3 size) const;
    ImageF project(int2 size, mat4 projection) const;

    buffer<Ellipsoid> ellipsoids;
};
