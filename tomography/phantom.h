#pragma once
#include "matrix.h"
#include "volume.h"

struct Ellipsoid {
    mat3 forward;
    mat3 inverse;
    vec3 center;
    float value;
    explicit Ellipsoid(mat3 forward, mat3 inverse, vec3 center, float value) : forward(forward), inverse(inverse), center(center), value(value) {}
    Ellipsoid(vec3 scale, vec3 angles, vec3 center, float value);
    bool contains(vec3 point) const;
    float intersect(vec3 point, vec3 direction) const;
};

struct Phantom {
    Phantom(uint count=0);
    VolumeF volume(int3 size) const;
    ImageF project(int2 size, mat4 projection) const;

    array<Ellipsoid> ellipsoids;
};
