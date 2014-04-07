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
    vec3 worldRay = stepSize * normalize( projection.transpose() * vec3(0,0,1) );
    float a = worldRay.x*worldRay.x+worldRay.y*worldRay.y;
    // FIXME: profile
    v4df ray = {worldRay.x, worldRay.y, worldRay.z, 1};
    v4df rayZ = double4(worldRay.z);
    v4df raySlopeZ = double4(1/worldRay.z);
    v4df rayXYXY = {worldRay.x, worldRay.y, worldRay.x, worldRay.y};
    v4df _m4a_4_m4a_4 = {-4*a, 4, -4*a, 4};
    v4df rcp_2a = double4(a ? -1./(2*a) : inf);
};

void project(const ImageP& image, const VolumeP& volume, Projection projection);

#if SIRT
struct SIRT {
    VolumeF p;
    VolumeF x;
    SIRT(uint N) : p(N), x(N) {}
    void initialize(const ref<Projection>&, const ref<ImageF>&) {}
    void step(const ref<Projection>& projections, const ref<ImageF>& images);
};
#endif

struct CGNR {
    VolumeP r, p, AtAp, x;
    real residualEnergy = 0;
    CGNR(uint N) : r(N), p(N), AtAp(N), x(N) {}
    void initialize(const ref<Projection>& projections, const ref<ImageP>& images);
    void residual(const ref<Projection>& projections, const ref<ImageP>& images, const VolumeP& additionalTarget);
    void step(const ref<Projection>& projections, const ref<ImageP>& images);
};
