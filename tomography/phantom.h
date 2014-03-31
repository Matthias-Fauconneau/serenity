#pragma once
#include "matrix.h"
#include "volume.h"

struct Ellipsoid : mat4 {
    float value;
    Ellipsoid(const mat4& m, float value) : mat4(m), value(value) {}
    Ellipsoid(float value, vec3 scale, vec3 origin, vec3 angles);
    Ellipsoid inverse(const mat4& m) const;
    bool contains(vec3 point) const { return norm(*this*point) < 1; }
    bool contains(vec2 point) const { return norm(*this*point) < 1; }
};

struct Phantom {
    Phantom();
    VolumeF volume(int3 size) const;
    ImageF project(int2 size, mat4 projection) const;

    buffer<Ellipsoid> ellipsoids;
};
