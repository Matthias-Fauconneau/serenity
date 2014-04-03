#pragma once
#include "matrix.h"
#include "volume.h"
#include "simd.h"

struct Projection {
    mat4 projection;
    Projection(mat4 projection) : projection(projection) {}

    // Precomputed parameters
    mat4 world = projection.inverse(); // Transform normalized view space to world space
    float stepSize = 1; // FIXME
    vec3 worldRay = stepSize * normalize( projection.transpose() * vec3(0,0,1) );
    float a = worldRay.x*worldRay.x+worldRay.y*worldRay.y;
    // FIXME: profile
    v4sf ray = {worldRay.x, worldRay.y, worldRay.z, stepSize};
    v4sf rayZ = float4(worldRay.z);
    v4sf raySlopeZ = float4(1/worldRay.z);
    v4sf rayXYXY = {worldRay.x, worldRay.y, worldRay.x, worldRay.y};
    v4sf _m4a_4_m4a_4 = {-4*a, 4, -4*a, 4};
    v4sf rcp_2a = float4(-1./(2*a));
};

void project(const ImageF& image, const VolumeF& volume, Projection projection);

struct SIRT {
    VolumeF p;
    VolumeF x;
    SIRT(uint N) : p(N), x(N) {}
    void step(const ref<Projection>& projections, const ref<ImageF>& images);
};

struct CGNR {
    VolumeF r, p, AtAp, x;
    real residual = 0;
    CGNR(uint N) : r(N), p(N), AtAp(N), x(N) {}
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images);
    void step(const ref<Projection>& projections);
};
