#pragma once
#include "matrix.h"
#include "volume.h"
#include "simd.h"
#include "thread.h"

struct Projection {
    Projection(mat4 projection, int2 imageSize) {
        // Intermediate parameters
        mat4 world = projection.inverse(); // Transform normalized view space to world space
        static constexpr float stepSize = 1;
        vec3 ray3 = stepSize * normalize( projection.transpose()[2].xyz() );
        float a = ray3.x*ray3.x+ray3.y*ray3.y;

        // Precomputed parameters
        origin = world * vec3(-vec2(imageSize-int2(1))/2.f, 0);
        xAxis = world[0];
        yAxis = world[1];
        raySlopeZ = float4(1/ray3.z);
        rayXYXY = (v4sf){ray3.x, ray3.y, ray3.x, ray3.y};
        _m4a_4_m4a_4 = (v4sf){-4*a, 4, -4*a, 4};
        rcp_2a = float4(-1./(2*a));
        rayZ = float4(ray3.z);
        ray = (v4sf){ray3.x, ray3.y, ray3.z, 1};
        ray8 = dup(ray);
    }

    // Precomputed parameters (11x4)
    v4sf origin, xAxis, yAxis;
    v4sf raySlopeZ, rayXYXY, _m4a_4_m4a_4, rcp_2a, rayZ, ray;
    v8sf ray8;
};

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& volume, const Projection& projection);

static constexpr uint threadCount = 8;

struct SIRT {
    buffer<v2hi> zOrder2;
    VolumeF x, p[threadCount];
    SIRT(uint N) : x(N), p{N} {}
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images);
    bool step(const ref<Projection>& projections, const ref<ImageF>& images);
};

struct CGNR {
    buffer<v2hi> zOrder2;
    VolumeF x,  AtAp[threadCount+1], p, r;

    real residualEnergy = 0;
    uint k = 0;
    CGNR(uint N) : x(N), p(N), r(N) { for(VolumeF& e: AtAp) e = VolumeF(N); }
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images);
    bool step(const ref<Projection>& projections, const ref<ImageF>& images);
};
