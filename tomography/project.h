#pragma once
#include "matrix.h"
#include "volume.h"
#include "simd.h"
#include "thread.h"
#include "time.h"

#define APPROXIMATE 1 // Approximate adjoint using bilinear sample instead of trilinear scatter

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

#if APPROXIMATE
        this->projection = projection;
#endif
    }

    // Precomputed parameters (11x4)
    v4sf origin, xAxis, yAxis;
    v4sf raySlopeZ, rayXYXY, _m4a_4_m4a_4, rcp_2a, rayZ, ray;
#if APPROXIMATE
    mat4 projection;
#endif
};

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& volume, const Projection& projection);

struct Reconstruction {
    uint k = 0;
    Time totalTime;
#if ZORDER2
    buffer<v2hi> zOrder2;
#endif
    VolumeF x;
    Reconstruction(uint N) : x(N) {}
    virtual ~Reconstruction() {}
    virtual void initialize(const ref<Projection>& projections, const ref<ImageF>& images) abstract;
    virtual bool step(const ref<Projection>& projections, const ref<ImageF>& images) abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.totalTime < b.totalTime; }
inline String str(const Reconstruction& r) { return str(r.k, r.totalTime); }

struct CGNR : Reconstruction  {
    real residualEnergy = 0;
    VolumeF p, r;
#if !APPROXIMATE
    VolumeF AtAp[threadCount];
#else
    VolumeF AtAp[1];
#endif
    tsc AtApTime, mergeTime, updateTime, nextTime;

    CGNR(uint N) : Reconstruction(N), p(N), r(N) { for(VolumeF& e: AtAp) e = VolumeF(N); }
    void initialize(const ref<Projection>& projections, const ref<ImageF>& images) override;
    bool step(const ref<Projection>& projections, const ref<ImageF>& images) override;
};
