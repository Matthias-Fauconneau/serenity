#pragma once
#include "matrix.h"
#include "volume.h"
#include "simd.h"

struct Projection {
    mat4 projection;
    Projection(mat4 projection) : projection(projection) {}

    // Precomputed parameters
    mat4 world = projection.inverse(); // Transform normalized view space to world space
    float stepSize = 1./2;
    vec3 ray3 = stepSize * normalize( projection.transpose() * vec3(0,0,1) );
    v4sf ray = {ray3.x, ray3.y, ray3.z, 1};
    v8sf ray8 = dup(ray);
    float a = ray3.x*ray3.x+ray3.y*ray3.y;
    v4sf rayZ = float4(ray3.z);
    v4sf raySlopeZ = float4(1/ray3.z);
    v4sf rayXYXY = {ray3.x, ray3.y, ray3.x, ray3.y};
    v4sf _m4a_4_m4a_4 = {-4*a, 4, -4*a, 4};
    v4sf rcp_2a = float4(a ? -1./(2*a) : inf);
};

void project(const ImageF& image, const VolumeF& volume, Projection projection);

struct CGNR {
    VolumeF r, p, AtAp, x;
    real residualEnergy = 0, deltaEnergy = 0;
    uint k = 0;
    CGNR(uint N) : r(N), p(N), AtAp(N), x(N) {}
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images);
    bool step(const ref<Projection>& projections, const ref<ImageF>& images);
};
