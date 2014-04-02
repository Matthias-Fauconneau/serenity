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

struct CylinderVolume {
    const VolumeF& volume;
    // Precomputed parameters
    int3 size = volume.sampleCount;
    const float radius = size.x/2-1, halfHeight = size.z/2-1; // Cylinder parameters
    const v4sf capZ = {halfHeight, halfHeight, -halfHeight, -halfHeight};
    const v4sf radiusSqHeight = {radius*radius, radius*radius, halfHeight, halfHeight};
    const v4sf radiusR0R0 = {radius*radius, 0, radius*radius, 0};
    const v4sf volumeOrigin = {(float)size.x/2, (float)size.y/2, (float)size.z/2, 0};
    float* const volumeData = volume;
    const uint64* const offsetX = volume.offsetX.data;
    const uint64* const offsetY = volume.offsetY.data;
    const uint64* const offsetZ = volume.offsetZ.data;

    CylinderVolume(const VolumeF& volume) : volume(volume) { assert_(volume.tiled() && size.x == size.y); }
    float accumulate(const Projection& p, const v4sf origin, float& length);
};

void project(const ImageF& image, CylinderVolume volume, Projection projection);

void update(CylinderVolume volume, const ref<Projection>& projections, const ref<ImageF>& images);
