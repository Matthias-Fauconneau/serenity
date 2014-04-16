#include "project.h"
#include "thread.h"

// SIMD constants for intersections
#define FLT_MAX __FLT_MAX__
static const v4sf _2f = float4( 2 );
static const v4sf mfloatMax = float4(-FLT_MAX);
static const v4sf floatMax = float4(FLT_MAX);
static const v4sf signPPNN = (v4sf)(v4si){0,0,(int)0x80000000,(int)0x80000000};
static const v4sf floatMMMm = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX};
const v8si _11101110 = (v8si){~0ll,~0ll,~0ll,0ll, ~0ll,~0ll,~0ll,0ll};
const v8sf _00001111f = (v8sf){0,0,0,0,1,1,1,1};

struct CylinderVolume {
    CylinderVolume(const VolumeF& volume) {
        assert_(volume.tiled() && size.x == size.y);
        const float radius = (size.x-1-1)/2, halfHeight = (size.z-1-1)/2; // Cylinder parameters (N-1 [domain size] - 1 (linear))
        capZ = (v4sf){halfHeight, halfHeight, -halfHeight, -halfHeight};
        radiusR0R0 = (v4sf){radius*radius, 0, radius*radius, 0};
        radiusSqHeight = (v4sf){radius*radius, radius*radius, halfHeight, halfHeight};
        dataOrigin = dup(float4(float(size.x-1)/2, float(size.y-1)/2 + (volume.offsetY.data-volume.offsetX.data), float(size.z-1)/2 + (volume.offsetZ.data-volume.offsetX.data), 0)) + float8(0,0,0,0,1,1,1,0);
        zOrder = volume.offsetX.data;
        data = volume;
        size = volume.sampleCount;
    }

    // Precomputed parameters
    v4sf capZ;
    v4sf radiusR0R0;
    v4sf radiusSqHeight;
    v8sf dataOrigin;

    const int32* zOrder;
    float* data;
    int3 size;

    /*/// Computes ray - cylinder intersection and integrates ray using uniform sampling with trilinear interpolation
    inline float project(const Projection& p, v4sf origin, v8sf& pStart, v4sf& tEnd, float& length);
    /// Convenience method to intersect and integrate (without returning intersections parameters)
    inline float project(const Projection& p, v4sf origin) { v8sf pStart; v4sf tEnd; float length; return project(p, origin, pStart, tEnd, length); }
    /// Ray backprojection using uniform sampling with trilinear interpolation
    inline void backproject(float* volumeData, const Projection& p, v8sf pStart, v4sf tEnd, float value);*/
};

#if 0
inline float CylinderVolume::project(const Projection& p, v4sf origin, v8sf& pStart, v4sf& tEnd, float& length) {
    // Intersects cap disks
    const v4sf originZ = shuffle4(origin, origin, 2,2,2,2);
    const v4sf capT = (capZ - originZ) * p.raySlopeZ; // top top bottom bottom
    const v4sf originXYXY = shuffle4(origin, origin, 0,1,0,1);
    const v4sf capXY = originXYXY + capT * p.rayXYXY;
    const v4sf capR = dot2(capXY, capXY); // top bottom top bottom
    // Intersect cylinder side
    const v4sf originXYrayXY = shuffle4(origin, p.ray, 0,1,0,1); // Ox Oy Dx Dy
    const v4sf cbcb = dot2(originXYXY, originXYrayXY); // OO OD OO OD (b=2OD)
    const v4sf _1b1b = blendps(_1f, cbcb, 0b1010); // 1 OD 1 OD
    const v4sf _4ac_bb = p._m4a_4_m4a_4 * (cbcb*_1b1b - radiusR0R0); // -4ac bb -4ac bb
    v4sf delta = hadd(_4ac_bb,_4ac_bb);
    const v4sf sqrtDelta = sqrt( delta );
    const v4sf sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
    const v4sf sideT = (_2f*_1b1b + sqrtDeltaPPNN) * p.rcp_2a; // ? t+ ? t-
    const v4sf sideZ = abs(originZ + sideT * p.rayZ); // ? z+ ? z-
    const v4sf capSideP = shuffle4(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
    const v4sf tMask = capSideP < radiusSqHeight;
    if(!mask(tMask)) { length=0; return 0; }
    const v4sf capSideT = shuffle4(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
    v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
    v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
    origin += tmin * p.ray;
    pStart = dataOrigin + dup(origin);
    tEnd = max(floatMMMm, tmax); // max, max, max, tmax
    length = floor(tmax[0]-tmin[0]); // Ray length (in steps) = Weight sum
    float Ax = 0; // Accumulates along the ray
    for(v8sf p01f = pStart;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
        // Converts {position, position+1} to integer coordinates
        const v8si p01 = cvttps2dq(p01f);
        assert_(p01[0]>=0 && p01[0]<size[0] && p01[1]-(offsetY-offsetX)>=0 && p01[1]-(offsetY-offsetX)<size[1] && p01[2]-(offsetZ-offsetX)>=0 && p01[2]-(offsetZ-offsetX)<size[2], p01[0], p01[1], p01[2]);
        assert_(p01[4]>=0 && p01[4]<size[0] && p01[5]-(offsetY-offsetX)>=0 && p01[5]-(offsetY-offsetX)<size[1] && p01[6]-(offsetZ-offsetX)>=0 && p01[6]-(offsetZ-offsetX)<size[2], p01[4], p01[5], p01[6]);
        // Lookups sample offsets
        const v8si offsetXYZXYZ = gather(offsetX, p01, _11101110);
        const v8si v01 = shuffle8(offsetXYZXYZ,offsetXYZXYZ, 0,0,0,0, 4,4,4,4) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 1,1, 5,5, 1,1, 5,5) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 2,6, 2,6, 2,6, 2,6);
        // Gather samples
        const v8sf cx01 = gather(volumeData, v01);
        // Computes trilinear interpolation coefficients
        const v8sf w_1mw = abs(p01f - cvtdq2ps(p01) - _00001111f); // fract(x), 1-fract(x)
        const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
        const float value = dot8(w01, cx01);
        Ax += value; // Accumulates trilinearly interpolated sample
        p01f += p.ray8; // Step
        if(mask(low(p01f) > tEnd)) return Ax;
    }
}

inline void CylinderVolume::backproject(float* volumeData, const Projection& p, v8sf p01f, v4sf tEnd, float value) {
    v8sf value8 = float8(value);
    for(;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
        // Converts {position, position+1} to integer coordinates
        const v8si p01 = cvttps2dq(p01f);
        assert_(p01[0]>=0 && p01[0]<size[0] && p01[1]-(offsetY-offsetX)>=0 && p01[1]-(offsetY-offsetX)<size[1] && p01[2]-(offsetZ-offsetX)>=0 && p01[2]-(offsetZ-offsetX)<size[2], p01[0], p01[1], p01[2]);
        assert_(p01[4]>=0 && p01[4]<size[0] && p01[5]-(offsetY-offsetX)>=0 && p01[5]-(offsetY-offsetX)<size[1] && p01[6]-(offsetZ-offsetX)>=0 && p01[6]-(offsetZ-offsetX)<size[2], p01[4], p01[5], p01[6]);
        // Lookups sample offsets
        const v8si offsetXYZXYZ = gather(offsetX, p01, _11101110);
        const v8si v01 = shuffle8(offsetXYZXYZ,offsetXYZXYZ, 0,0,0,0, 4,4,4,4) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 1,1, 5,5, 1,1, 5,5) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 2,6, 2,6, 2,6, 2,6);
        // Gather samples
        v8sf cx01 = gather(volumeData, v01);
        // Computes trilinear interpolation coefficients
        const v8sf w_1mw = abs(p01f - cvtdq2ps(p01) - _00001111f); // fract(x), 1-fract(x)
        const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
       // Update
        cx01 += w01 * value8; // cx01 = max(_0f, cx01) ?
        // Scatter ~ scatter(data, v01, cx01);
        volumeData[v01[0]] = cx01[0];
        volumeData[v01[1]] = cx01[1];
        volumeData[v01[2]] = cx01[2];
        volumeData[v01[3]] = cx01[3];
        volumeData[v01[4]] = cx01[4];
        volumeData[v01[5]] = cx01[5];
        volumeData[v01[6]] = cx01[6];
        volumeData[v01[7]] = cx01[7];
        p01f += p.ray8; // Step
        if(mask(low(p01f) > tEnd)) return;
    }
}
#endif

void project(const ImageF& image, const VolumeF& source, Projection projection) {
    CylinderVolume volume(source);
    float* const imageData = (float*)image.data;
    uint imageWidth = image.width;
    parallel(image.data.size, [projection, volume, imageData, imageWidth](uint, uint pixelIndex) {
        int2 pixelPosition = zOrder2(pixelIndex); // Assumes square POT image
        v4sf origin = projection.origin + float4(pixelPosition[0]) * projection.xAxis + float4(pixelPosition[1]) * projection.yAxis;
        //imageData[pixelPosition[1]*image.width+pixelPosition[0]] = source.project(projection, origin);
        float& pixel = imageData[pixelPosition[1]*imageWidth+pixelPosition[0]];
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
        v4sf delta = hadd(_4ac_bb,_4ac_bb);
        const v4sf sqrtDelta = sqrt( delta );
        const v4sf sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
        const v4sf sideT = (_2f*_1b1b + sqrtDeltaPPNN) * projection.rcp_2a; // ? t+ ? t-
        const v4sf sideZ = abs(originZ + sideT * projection.rayZ); // ? z+ ? z-
        const v4sf capSideP = shuffle4(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
        const v4sf tMask = capSideP < volume.radiusSqHeight;
        if(!mask(tMask)) { pixel=0; return; }
        const v4sf capSideT = shuffle4(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
        v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
        v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
        origin += tmin * projection.ray;
        v8sf pStart = volume.dataOrigin + dup(origin);
        v4sf tEnd = max(floatMMMm, tmax); // max, max, max, tmax
        float Ax = 0; // Accumulates along the ray
        for(v8sf p01f = pStart;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
            // Converts {position, position+1} to integer coordinates
            const v8si p01 = cvttps2dq(p01f);
            // Lookups sample offsets
            const v8si offsetXYZXYZ = gather(volume.zOrder, p01, _11101110);
            const v8si v01 = shuffle8(offsetXYZXYZ,offsetXYZXYZ, 0,0,0,0, 4,4,4,4) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 1,1, 5,5, 1,1, 5,5) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 2,6, 2,6, 2,6, 2,6);
            // Gather samples
            const v8sf cx01 = gather(volume.data, v01);
            // Computes trilinear interpolation coefficients
            const v8sf w_1mw = abs(p01f - cvtdq2ps(p01) - _00001111f); // fract(x), 1-fract(x)
            const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
            const float value = dot8(w01, cx01);
            Ax += value; // Accumulates trilinearly interpolated sample
            p01f += projection.ray8; // Step
            if(mask(low(p01f) > tEnd)) break;
        }
        pixel = Ax;
    } );
}

/// Computes residual r = p = At ( b - A x )
void CGNR::initialize(const ref<Projection>& projections, const ref<ImageF>& images) {
    int2 imageSize = images.first().size();
    zOrder2 = buffer<v2hi>(imageSize.y * imageSize.x); // Large lookup table (~512K) but read sequentially (stream read, skip cache)
    for(uint index: range(zOrder2.size)) { short2 p = short2(::zOrder2(index)); zOrder2[index] = (v2hi){p.x, p.y}; } // Generates lookup table (assumes square POT images)

    assert(projections.size == images.size);
    CylinderVolume volume (x);
    float* pData = (float*)p.data.data;
    float* rData = (float*)r.data.data;
    const v2hi* zOrder2Data = zOrder2.data;
    for(uint projectionIndex: range(projections.size)) { // TODO: Cluster rays for coherence (even from different projections)
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        float* const imageData = (float*)image.data;
        uint imageWidth = image.width;
        parallel(image.data.size, [zOrder2Data,projection,imageData,imageWidth,volume](uint, uint pixelIndex) { // Process pixels in zOrder for coherence (TODO: 2x2x2 ray packets)
            v2hi pixelPosition = zOrder2Data[pixelIndex];
            v4sf origin = projection.origin + float4(pixelPosition[0]) * projection.xAxis + float4(pixelPosition[1]) * projection.yAxis;
            float b = imageData[pixelPosition[1]*imageWidth+pixelPosition[0]];
#if 0
            v8sf pStart; v4sf tEnd;
            float length; float Ax = volume.project(projection, origin, pStart, tEnd, length);
            if(length) {
                float p = (b - Ax) / length;
                volume.backproject(pData, projection, pStart, tEnd, p);
            }
#else
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
            v4sf delta = hadd(_4ac_bb,_4ac_bb);
            const v4sf sqrtDelta = sqrt( delta );
            const v4sf sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
            const v4sf sideT = (_2f*_1b1b + sqrtDeltaPPNN) * projection.rcp_2a; // ? t+ ? t-
            const v4sf sideZ = abs(originZ + sideT * projection.rayZ); // ? z+ ? z-
            const v4sf capSideP = shuffle4(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
            const v4sf tMask = capSideP < volume.radiusSqHeight;
            if(!mask(tMask)) return;
            const v4sf capSideT = shuffle4(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
            v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
            v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
            origin += tmin * projection.ray;
            v8sf pStart = volume.dataOrigin + dup(origin);
            v4sf tEnd = max(floatMMMm, tmax); // max, max, max, tmax
            float length = floor(tmax[0]-tmin[0]); // Ray length (in steps) = Weight sum
            float Ax = 0; // Accumulates along the ray
            for(v8sf p01f = pStart;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
                // Converts {position, position+1} to integer coordinates
                const v8si p01 = cvttps2dq(p01f);
                //assert_(p01[0]>=0 && p01[0]<volume.size[0] && p01[1]-(volume.offsetY-volume.offsetX)>=0 && p01[1]-(volume.offsetY-volume.offsetX)<volume.size[1] && p01[2]-(volume.offsetZ-volume.offsetX)>=0 && p01[2]-(volume.offsetZ-volume.offsetX)<volume.size[2], p01[0], p01[1], p01[2]);
                //assert_(p01[4]>=0 && p01[4]<volume.size[0] && p01[5]-(volume.offsetY-volume.offsetX)>=0 && p01[5]-(volume.offsetY-volume.offsetX)<volume.size[1] && p01[6]-(volume.offsetZ-volume.offsetX)>=0 && p01[6]-(volume.offsetZ-volume.offsetX)<volume.size[2], p01[4], p01[5], p01[6]);
                // Lookups sample offsets
                const v8si offsetXYZXYZ = gather(volume.zOrder, p01, _11101110);
                const v8si v01 = shuffle8(offsetXYZXYZ,offsetXYZXYZ, 0,0,0,0, 4,4,4,4) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 1,1, 5,5, 1,1, 5,5) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 2,6, 2,6, 2,6, 2,6);
                // Gather samples
                const v8sf cx01 = gather(volume.data, v01);
                // Computes trilinear interpolation coefficients
                const v8sf w_1mw = abs(p01f - cvtdq2ps(p01) - _00001111f); // fract(x), 1-fract(x)
                const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
                const float value = dot8(w01, cx01);
                Ax += value; // Accumulates trilinearly interpolated sample
                p01f += projection.ray8; // Step
                if(mask(low(p01f) > tEnd)) break;
            }
            float value = (b - Ax) / length;
            v8sf value8 = float8(value);
            for(v8sf p01f = pStart;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
                // Converts {position, position+1} to integer coordinates
                const v8si p01 = cvttps2dq(p01f);
                //assert_(p01[0]>=0 && p01[0]<volume.size[0] && p01[1]-(volume.offsetY-volume.offsetX)>=0 && p01[1]-(volume.offsetY-volume.offsetX)<volume.size[1] && p01[2]-(volume.offsetZ-volume.offsetX)>=0 && p01[2]-(volume.offsetZ-volume.offsetX)<volume.size[2], p01[0], p01[1], p01[2]);
                //assert_(p01[4]>=0 && p01[4]<volume.size[0] && p01[5]-(volume.offsetY-volume.offsetX)>=0 && p01[5]-(volume.offsetY-volume.offsetX)<volume.size[1] && p01[6]-(volume.offsetZ-volume.offsetX)>=0 && p01[6]-(volume.offsetZ-volume.offsetX)<volume.size[2], p01[4], p01[5], p01[6]);
                // Lookups sample offsets
                const v8si offsetXYZXYZ = gather(volume.zOrder, p01, _11101110);
                const v8si v01 = shuffle8(offsetXYZXYZ,offsetXYZXYZ, 0,0,0,0, 4,4,4,4) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 1,1, 5,5, 1,1, 5,5) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 2,6, 2,6, 2,6, 2,6);
                // Gather samples
                v8sf cx01 = gather(volume.data, v01);
                // Computes trilinear interpolation coefficients
                const v8sf w_1mw = abs(p01f - cvtdq2ps(p01) - _00001111f); // fract(x), 1-fract(x)
                const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
                // Update
                cx01 += w01 * value8; // cx01 = max(_0f, cx01) ?
                // Scatter ~ scatter(data, v01, cx01);
                volume.data[v01[0]] = cx01[0]; volume.data[v01[1]] = cx01[1]; volume.data[v01[2]] = cx01[2]; volume.data[v01[3]] = cx01[3];
                volume.data[v01[4]] = cx01[4]; volume.data[v01[5]] = cx01[5]; volume.data[v01[6]] = cx01[6]; volume.data[v01[7]] = cx01[7];
                p01f += projection.ray8; // Step
                if(mask(low(p01f) > tEnd)) return;
            }
#endif
        } );
    }
    // Copies r=p and computes |r|
    float residualSum[coreCount] = {};
    chunk_parallel(p.size(), [&](uint id, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            float p = pData[index];
            rData[index] = p;
            residualSum[id] += sq(p);
        }
    });
    residualEnergy = sum(residualSum);
    log(residualEnergy);
    assert_(residualEnergy);
}

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + At α p[k]
bool CGNR::step(const ref<Projection>& projections, const ref<ImageF>& images) {
    k++;
    CylinderVolume volume (p);
    float* pData = (float*)p.data.data; // p
    float* AtApData = (float*)AtAp.data.data; // At A p
    const v2hi* zOrder2Data = zOrder2.data;
    // TODO: Gathers all projections from p, then backproject (gather-scatter) in AtAp (i.e process one volume per pass) |OR| Interleave p and AtAp (half cache hit/miss if full ray fits) (+no need to allocate memory for all projections)
    for(uint projectionIndex: range(projections.size)) { // TODO: Cluster rays for coherence (even from different projections)
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        parallel(image.data.size, [projection,volume,zOrder2Data](uint, uint pixelIndex) { // Process pixels in zOrder for coherence (TODO: 2x2x2 ray packets)
            v2hi pixelPosition = zOrder2Data[pixelIndex];
            v4sf origin = projection.origin + float4(pixelPosition[0]) * projection.xAxis + float4(pixelPosition[1]) * projection.yAxis;
#if 0
            v8sf pStart; v4sf tEnd;
            float length; float Ap = volume.project(projection, origin, pStart, tEnd, length);
            if(length) volume.backproject(AtApData, projection, pStart, tEnd, Ap / length); // AtAp
#else
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
            v4sf delta = hadd(_4ac_bb,_4ac_bb);
            const v4sf sqrtDelta = sqrt( delta );
            const v4sf sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
            const v4sf sideT = (_2f*_1b1b + sqrtDeltaPPNN) * projection.rcp_2a; // ? t+ ? t-
            const v4sf sideZ = abs(originZ + sideT * projection.rayZ); // ? z+ ? z-
            const v4sf capSideP = shuffle4(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
            const v4sf tMask = capSideP < volume.radiusSqHeight;
            if(!mask(tMask)) return;
            const v4sf capSideT = shuffle4(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
            v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
            v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
            origin += tmin * projection.ray;
            v8sf pStart = volume.dataOrigin + dup(origin);
            v4sf tEnd = max(floatMMMm, tmax); // max, max, max, tmax
            float length = floor(tmax[0]-tmin[0]); // Ray length (in steps) = Weight sum
            float Ap = 0; // Accumulates along the ray
            for(v8sf p01f = pStart;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
                // Converts {position, position+1} to integer coordinates
                const v8si p01 = cvttps2dq(p01f);
                //assert_(p01[0]>=0 && p01[0]<volume.size[0] && p01[1]-(volume.offsetY-volume.offsetX)>=0 && p01[1]-(volume.offsetY-volume.offsetX)<volume.size[1] && p01[2]-(volume.offsetZ-volume.offsetX)>=0 && p01[2]-(volume.offsetZ-volume.offsetX)<volume.size[2], p01[0], p01[1], p01[2]);
                //assert_(p01[4]>=0 && p01[4]<volume.size[0] && p01[5]-(volume.offsetY-volume.offsetX)>=0 && p01[5]-(volume.offsetY-volume.offsetX)<volume.size[1] && p01[6]-(volume.offsetZ-volume.offsetX)>=0 && p01[6]-(volume.offsetZ-volume.offsetX)<volume.size[2], p01[4], p01[5], p01[6]);
                // Lookups sample offsets
                const v8si offsetXYZXYZ = gather(volume.zOrder, p01, _11101110);
                const v8si v01 = shuffle8(offsetXYZXYZ,offsetXYZXYZ, 0,0,0,0, 4,4,4,4) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 1,1, 5,5, 1,1, 5,5) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 2,6, 2,6, 2,6, 2,6);
                // Gather samples
                const v8sf cx01 = gather(volume.data, v01);
                // Computes trilinear interpolation coefficients
                const v8sf w_1mw = abs(p01f - cvtdq2ps(p01) - _00001111f); // fract(x), 1-fract(x)
                const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
                const float value = dot8(w01, cx01);
                Ap += value; // Accumulates trilinearly interpolated sample
                p01f += projection.ray8; // Step
                if(mask(low(p01f) > tEnd)) break;
            }
            float value = Ap / length;
            v8sf value8 = float8(value);
            for(v8sf p01f = pStart;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
                // Converts {position, position+1} to integer coordinates
                const v8si p01 = cvttps2dq(p01f);
                //assert_(p01[0]>=0 && p01[0]<volume.size[0] && p01[1]-(volume.offsetY-volume.offsetX)>=0 && p01[1]-(volume.offsetY-volume.offsetX)<volume.size[1] && p01[2]-(volume.offsetZ-volume.offsetX)>=0 && p01[2]-(volume.offsetZ-volume.offsetX)<volume.size[2], p01[0], p01[1], p01[2]);
                //assert_(p01[4]>=0 && p01[4]<volume.size[0] && p01[5]-(volume.offsetY-volume.offsetX)>=0 && p01[5]-(volume.offsetY-volume.offsetX)<volume.size[1] && p01[6]-(volume.offsetZ-volume.offsetX)>=0 && p01[6]-(volume.offsetZ-volume.offsetX)<volume.size[2], p01[4], p01[5], p01[6]);
                // Lookups sample offsets
                const v8si offsetXYZXYZ = gather(volume.zOrder, p01, _11101110);
                const v8si v01 = shuffle8(offsetXYZXYZ,offsetXYZXYZ, 0,0,0,0, 4,4,4,4) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 1,1, 5,5, 1,1, 5,5) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 2,6, 2,6, 2,6, 2,6);
                // Gather samples
                v8sf cx01 = gather(volume.data, v01);
                // Computes trilinear interpolation coefficients
                const v8sf w_1mw = abs(p01f - cvtdq2ps(p01) - _00001111f); // fract(x), 1-fract(x)
                const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
                // Update
                cx01 += w01 * value8; // cx01 = max(_0f, cx01) ?
                // Scatter ~ scatter(data, v01, cx01);
                volume.data[v01[0]] = cx01[0]; volume.data[v01[1]] = cx01[1]; volume.data[v01[2]] = cx01[2]; volume.data[v01[3]] = cx01[3];
                volume.data[v01[4]] = cx01[4]; volume.data[v01[5]] = cx01[5]; volume.data[v01[6]] = cx01[6]; volume.data[v01[7]] = cx01[7];
                p01f += projection.ray8; // Step
                if(mask(low(p01f) > tEnd)) return;
            }
#endif
        } );
    }
    float pAtApSum[coreCount] = {};
    // Computes |p·Atp|
    chunk_parallel(p.size(), [&](uint id, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            pAtApSum[id] += pData[index] * AtApData[index]; // p · At A p
        }
    });
    float pAtAp = sum(pAtApSum);
    assert(pAtAp);
    float alpha = residualEnergy / pAtAp;
    float* xData = (float*)x.data.data; // x
    float* rData = (float*)r.data.data; // r
    float deltaSum[coreCount] = {};
    float newResidualSum[coreCount] = {};
    chunk_parallel(p.size(), [&](uint id, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            float delta = alpha * pData[index];
            xData[index] += delta;
            deltaSum[id] += sq(delta);
            rData[index] -= alpha * AtApData[index];
            newResidualSum[id] += sq(rData[index]);
        }
    });
    float newResidual = sum(newResidualSum);
    float beta = newResidual / residualEnergy;
    float newDelta = sum(deltaSum);
    log(k,'\t',residualEnergy,'\\',newResidual,'=',beta,'\t',deltaEnergy,'\\',newDelta,'=',newDelta/deltaEnergy);
    //if(beta > 1) return false; // FIXME: stop before last update
    // Computes next search direction
    chunk_parallel(p.size(), [&](uint, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            pData[index] = rData[index] + beta * pData[index];
        }
    });
    residualEnergy = newResidual, deltaEnergy = newDelta;
    return true;
}
