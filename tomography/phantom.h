#pragma once
#include "matrix.h"

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
    buffer<float> volume(int3 size) const;
    void project(const struct ImageF& image, int3 volumeSize, const struct Projection& projection) const;

    array<Ellipsoid> ellipsoids;
};
