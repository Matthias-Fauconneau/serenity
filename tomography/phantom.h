#pragma once
#include "matrix.h"
#include "volume.h"
#include "project.h"

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
    void volume(const VolumeF& volume) const;
    void project(const ImageF& image, const Projection& projection) const;

    array<Ellipsoid> ellipsoids;
};
