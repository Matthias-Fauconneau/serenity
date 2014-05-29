#pragma once
#include "volume.h"
#include "matrix.h"

//inline vec3 toVec3(float4 v) { return vec3(v[0],v[1],v[2]); } // FIXME

struct Projection {
    //int3 volumeSize;
    int2 imageSize; //uint index;
    Projection(const int3 volumeSize, const int2 imageSize, const uint index, const uint projectionCount) : /*volumeSize(volumeSize),*/ imageSize(imageSize) /*, index(index) */ {
        // FIXME: parse from measurement file
        const uint image_height = 1536;
        const uint image_width = 2048;
        assert_(image_height*imageSize.x == image_width*imageSize.y, imageSize, image_height, image_width);
        const uint num_projections_per_revolution = 2520;
        const float camera_length = 328.811; // [mm]
        const float specimen_distance = 2.78845; // [mm]
        const float pixel_size = 0.194; // [mm]
        const float z_start_position = 32.1; // [mm]
        const float z_end_position = 37.3082; // [mm]
        const float deltaZ = (z_end_position-z_start_position) / 2 /* Half pitch: Z[end] is incorrect ?*/; // [mm] ~ 5 mm
        //const float pitch = deltaZ/total_num_projections*num_projections_per_revolution; // [mm] ~ 2.604 mm
        const float detectorHalfWidth = image_width * pixel_size; // [mm] ~ 397 mm
        const float hFOV = atan(detectorHalfWidth, camera_length); // Horizontal field of view (i.e cone beam angle) [rad] ~ 50°
        const float volumeRadius = specimen_distance * cos(hFOV); // [mm] ~ 2 mm
        const float voxelRadius = float(volumeSize.x-1)/2;

        mat3 rotation = mat3().rotateZ(2*PI*float(index)/num_projections_per_revolution);
        origin = rotation * vec3(-specimen_distance/volumeRadius*voxelRadius,0, (float(index)/float(projectionCount)*deltaZ)/volumeRadius*voxelRadius - volumeSize.z/2 + imageSize.y*voxelRadius/float(imageSize.x-1));
        /*ray[0] = rotation * vec3(0,2.f*voxelRadius/float(imageSize.x-1),0);
        ray[1] = rotation * vec3(0,0,2.f*voxelRadius/float(imageSize.x-1));
        ray[2] = rotation * vec3(specimen_distance/volumeRadius*voxelRadius,0,0) - float3((imageSize.x-1)/2.f)*ray[0] - float3((imageSize.y-1)/2.f)*ray[1];*/
        this->rotation[0] = rotation * vec3(0,2.f*voxelRadius,0);
        this->rotation[1] = rotation * vec3(0,0,2.f*voxelRadius);
        this->rotation[2] = rotation * vec3(specimen_distance/volumeRadius*voxelRadius,0,0); // - float3((imageSize.x-1)/2.f)*this->rotation[0] - float3((imageSize.y-1)/2.f)*this->rotation[1];
        //this->rotation = mat3().rotateZ( - 2*PI*float(index)/num_projections_per_revolution);
        //this->scale = float(imageSize.x-1)/voxelRadius;
    }
#if 0
    inline vec3 pixelRay(float x, float y) const { return toVec3(normalize3(float4(x) * ray[0] + float4(y) * ray[1] + ray[2])); }
    //inline float4 ray(float x, float y) { return  blendps(_1f, normalize3(float4(x) * ray[0] + float4(y) * ray[1] + ray[2]), 0b0111); }
    inline vec2 project(vec3 p) const {
        p = rotation * (p - vec3(origin[0],origin[1],origin[2]));
        assert(p.x, p);
        p = p / p.x; // Perspective divide
        return vec2(p.y * scale, p.z * scale);
    }
#endif

    float3 origin;
    //float3 ray[3];
    mat3 rotation;
    //float scale;
};

inline buffer<ImageF> sliceProjectionVolume(const VolumeF& volume, uint stride=1, bool downsampleProjections=false) {
    buffer<ImageF> images (volume.sampleCount.z / stride);
    for(int index: range(images.size)) new (images+index) ImageF(downsampleProjections ? downsample(slice(volume, index*stride)) : slice(volume, index*stride));
    return images;
}

inline buffer<Projection> evaluateProjections(int3 reconstructionSize, int2 imageSize, uint projectionCount, uint stride=1) {
    buffer<Projection> projections (projectionCount / stride);
    for(int index: range(projections.size)) projections[index] = Projection(reconstructionSize, imageSize, index*stride, projectionCount);
    return projections;
}
#if 0
// SIMD constants for intersections
#define FLT_MAX __FLT_MAX__
static const float4 _2f = float4( 2 );
static const float4 _m2f = float4( -2 );
static const float4 mfloatMax = float4(-FLT_MAX);
static const float4 floatMax = float4(FLT_MAX);
static const float4 signPPNN = (float4)(v4si){0,0,(int)0x80000000,(int)0x80000000};
static const float4 floatMMMm = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX};
static const v8sf _00001111f = (v8sf){0,0,0,0,1,1,1,1};
static const float4 _0001f = (float4){0,0,0,1};
static const float4 m4_0_m4_0 = (float4){-4,0,-4,0};
static const float4 _0404f = (float4){0,4,0,4};
static inline bool intersect(const Projection& projection, vec2 pixelPosition, const CylinderVolume& volume, float4& start, float4& ray, float4& end) {
    /// Intersects
    const float4 origin = projection.origin;
    ray =  blendps(_1f, normalize3(float4(pixelPosition.x) * projection.ray[0] + float4(pixelPosition.y) * projection.ray[1] + projection.ray[2]), 0b0111);

    // Intersects cap disks
    const float4 originZ = shuffle4(origin, origin, 2,2,2,2);
    const float4 capT = (volume.capZ - originZ) * rcp(shuffle4(ray, ray, 2,2,2,2)); // top top bottom bottom
    const float4 originXYXY = shuffle4(origin, origin, 0,1,0,1);
    const float4 rayXYXY = shuffle4(ray, ray, 0,1,0,1);
    const float4 capXY = originXYXY + capT * rayXYXY;
    const float4 capR = dot2(capXY, capXY); // top bottom top bottom
    // Intersect cylinder side
    const float4 originXYrayXY = shuffle4(origin, ray, 0,1,0,1); // Ox Oy Dx Dy
    const float4 cbcb = dot2(originXYXY, originXYrayXY); // OO OD OO OD (b=2OD)
    const float4 _1b1b = blendps(_1f, cbcb, 0b1010); // 1 OD 1 OD
    const float4 a = dot2(rayXYXY, rayXYXY); // x²+y²
    const float4 _4ac_bb = (m4_0_m4_0 * a + _0404f) * (cbcb*_1b1b - volume.radiusR0R0); // -4ac 4bb -4ac 4bb
    const float4 delta = hadd(_4ac_bb,_4ac_bb);
    const float4 sqrtDelta = sqrt( delta );
    const float4 sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
    const float4 sideT = (_2f*_1b1b + sqrtDeltaPPNN) *  rcp(_m2f*a); // ? t+ ? t-
    const float4 sideZ = abs(originZ + sideT * float4(ray[2])); // ? z+ ? z-
    const float4 capSideP = shuffle4(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
    const int4 tMask = capSideP < volume.radiusSqHeight;
    if(!mask(tMask)) { /*start=_0f; end=_0f;*/ return false; }
    const float4 capSideT = shuffle4(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
    float4 tmin = hmin( blendv(floatMax, capSideT, tMask) );
    float4 tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
    start = volume.dataOrigin + origin + tmin * ray;
    //if(!(int3(start[0], start[1], start[2])>=int3(0) && int3(start[0], start[1], start[2])<int3(volume.size))) return false; // DEBUG
    //assert_(int3(start[0], start[1], start[2])>=int3(0) && int3(start[0], start[1], start[2])<int3(volume.size), start, volume.size);
    end = max(floatMMMm, tmax); // max, max, max, tmax
    //float4 last = volume.dataOrigin + origin + tmax * ray;
    //assert_(int3(last[0], last[1], last[2])>=int3(0) && int3(last[0], last[1], last[2])<int3(volume.size), last, volume.size);
    return true;
}

/// Integrates value from voxels along ray
static inline float project(float4 position, float4 step, float4 end, const CylinderVolume& volume, const float* data) {
    for(float4 accumulator = _0f;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
        const v4si integerPosition = cvttps2dq(position); // Converts position to integer coordinates
        //assert_(int3(integerPosition[0], integerPosition[1], integerPosition[2])>=int3(0) && int3(integerPosition[0], integerPosition[1], integerPosition[2])<volume.size, position);
        const v4si index = dot4(integerPosition, volume.stride); // to voxel index
        const v4si indices = index + volume.offset; // to 4 voxel indices
        const v8sf samples = gather2(data, indices); // Gather samples
        const v8sf fract = abs(dup(position - cvtdq2ps(integerPosition)) - _00001111f); // Computes trilinear interpolation coefficients
        const v8sf weights = shuffle8(fract, fract, 4,4,4,4, 0,0,0,0) * shuffle8(fract, fract, 5,5, 1,1, 5,5, 1,1) * shuffle8(fract, fract, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
        accumulator += dot8(weights, samples); // Accumulates trilinearly interpolated sample
        position += step; // Step
        if(mask(position > end)) return accumulator[0];
    }
}
#endif

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& volume, const Projection& projection);
