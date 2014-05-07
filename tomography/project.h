#pragma once
#include "volume.h"
#include "matrix.h"

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
        this->projection = projection;
    }

    // Precomputed parameters (11x4)
    v4sf origin, xAxis, yAxis;
    v4sf raySlopeZ, rayXYXY, _m4a_4_m4a_4, rcp_2a, rayZ, ray;
    mat4 projection; // for approximate
};

// SIMD constants for intersections
#define FLT_MAX __FLT_MAX__
static const v4sf _2f = float4( 2 );
static const v4sf mfloatMax = float4(-FLT_MAX);
static const v4sf floatMax = float4(FLT_MAX);
static const v4sf signPPNN = (v4sf)(v4si){0,0,(int)0x80000000,(int)0x80000000};
static const v4sf floatMMMm = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX};
static const v8sf _00001111f = (v8sf){0,0,0,0,1,1,1,1};

static inline bool intersect(const Projection& projection, vec2 pixelPosition, const CylinderVolume& volume, v4sf& start, v4sf& end) {
    /// Intersects
    v4sf origin = projection.origin + float4(pixelPosition.x) * projection.xAxis + float4(pixelPosition.y) * projection.yAxis;
    // Intersects cap disks
    const v4sf originZ = shuffle4(origin, origin, 2,2,2,2);
    const v4sf capT = (volume.capZ - originZ) * projection.raySlopeZ; // top top bottom bottom
    const v4sf originXYXY = shuffle4(origin, origin, 0,1,0,1);
    const v4sf capXY = originXYXY + capT * projection.rayXYXY;
    const v4sf capR = dot2(capXY, capXY); // top bottom top bottom
    // Intersect cylinder side
    const v4sf originXYrayXY = shuffle4(origin, projection.ray, 0,1,0,1); // Ox Oy Dx Dy
    const v4sf cbcb = dot2(originXYXY, originXYrayXY); // OO OD OO OD (b=2OD)
    const v4sf _1b1b = blendps(_1f, cbcb, 0b1010); // 1 OD 1 OD
    const v4sf _4ac_bb = projection._m4a_4_m4a_4 * (cbcb*_1b1b - volume.radiusR0R0); // -4ac bb -4ac bb
    const v4sf delta = hadd(_4ac_bb,_4ac_bb);
    const v4sf sqrtDelta = sqrt( delta );
    const v4sf sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
    const v4sf sideT = (_2f*_1b1b + sqrtDeltaPPNN) * projection.rcp_2a; // ? t+ ? t-
    const v4sf sideZ = abs(originZ + sideT * projection.rayZ); // ? z+ ? z-
    const v4sf capSideP = shuffle4(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
    const v4sf tMask = capSideP <= volume.radiusSqHeight;
    if(!mask(tMask)) { /*start=_0f; end=_0f;*/ return false; }
    const v4sf capSideT = shuffle4(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
    v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
    v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
    start = volume.dataOrigin + origin + tmin * projection.ray;
    end = max(floatMMMm, tmax); // max, max, max, tmax
    return true;
}

/// Integrates value from voxels along ray
static inline float project(v4sf position, v4sf step, v4sf end, const CylinderVolume& volume, const float* data) {
    for(v4sf accumulator = _0f;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
        const v4si integerPosition = cvttps2dq(position); // Converts position to integer coordinates
        const v4si index = dot4(integerPosition, volume.stride); // to voxel index
        const v4si indices = index + volume.offset; // to 4 voxel indices
        assert_(uint(indices[0])<uint(volume.stride[2]*volume.stride[1]), indices);
        assert_(uint(indices[1])<uint(volume.stride[2]*volume.stride[1]), indices);
        assert_(uint(indices[2])<uint(volume.stride[2]*volume.stride[1]), indices);
        assert_(uint(indices[3])<uint(volume.stride[2]*volume.stride[1]), indices);
        const v8sf samples = gather2(data, indices); // Gather samples
        const v8sf fract = abs(dup(position - cvtdq2ps(integerPosition)) - _00001111f); // Computes trilinear interpolation coefficients
        const v8sf weights = shuffle8(fract, fract, 4,4,4,4, 0,0,0,0) * shuffle8(fract, fract, 5,5, 1,1, 5,5, 1,1) * shuffle8(fract, fract, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
        accumulator += dot8(weights, samples); // Accumulates trilinearly interpolated sample
        position += step; // Step
        if(mask(position > end)) return accumulator[0];
    }
}

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& volume, const mat4& projection);
