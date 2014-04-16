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
    const VolumeF& volume;
    // Precomputed parameters
    int3 size = volume.sampleCount;
    const float radius = (size.x-1-1)/2, halfHeight = (size.z-1-1)/2; // Cylinder parameters (N-1 [domain size] - 1 (linear))
    const v4sf capZ = {halfHeight, halfHeight, -halfHeight, -halfHeight};
    const v4sf radiusSqHeight = {radius*radius, radius*radius, halfHeight, halfHeight};
    const v4sf radiusR0R0 = {radius*radius, 0, radius*radius, 0};

    const int32* const offsetX = volume.offsetX.data;
    const int32* const offsetY = volume.offsetY.data;
    const int32* const offsetZ = volume.offsetZ.data;

    const v8sf dataOrigin = dup(float4(float(size.x-1)/2, float(size.y-1)/2 + (offsetY-offsetX), float(size.z-1)/2 + (offsetZ-offsetX), 0)) + float8(0,0,0,0,1,1,1,0);

    float* const volumeData = volume;

    CylinderVolume(const VolumeF& volume) : volume(volume) { assert_(volume.tiled() && size.x == size.y); }
    /// Computes ray - cylinder intersection
    inline float intersect(const Projection& p, v4sf origin, v8sf& pStart, v4sf& tEnd);
    /// Ray integration using uniform sampling with trilinear interpolation
    inline float project(const Projection& p, v8sf pStart, v4sf tEnd);
    /// Ray backprojection using uniform sampling with trilinear interpolation
    inline void backproject(float* volumeData, const Projection& p, v8sf pStart, v4sf tEnd, float value);

    /// Convenience method to intersect and integrate
    float project(const Projection& p, v4sf origin) {
        v8sf pStart; v4sf tEnd;
        float length = intersect(p, origin, pStart, tEnd);
        return length ? project(p, pStart, tEnd) : 0;
    }
};

inline float CylinderVolume::intersect(const Projection& p, v4sf origin, v8sf& pStart, v4sf& tEnd) {
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
    if(!mask(tMask)) return 0;
    const v4sf capSideT = shuffle4(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
    v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
    v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
    origin += tmin * p.ray;
    pStart = dataOrigin + dup(origin);
    tEnd = max(floatMMMm, tmax); // max, max, max, tmax
    return floor(tmax[0]-tmin[0]); // Ray length (in steps) = Weight sum
}

inline float CylinderVolume::project(const Projection& p, v8sf p01f, v4sf tEnd) {
    float Ax = 0; // Accumulates along the ray
    for(;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
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

void project(const ImageF& image, const VolumeF& volume, Projection projection) {
    CylinderVolume source(volume);

    // Image
    constexpr uint tileSize = 8;
    int imageX = floor(tileSize,image.width), imageY = floor(tileSize,image.height), imageStride = image.width; // Target image size
    assert_(imageX%tileSize ==0 && imageY%tileSize ==0);
    float* const imageData = (float*)image.data;
    vec3 vViewStepX = projection.world * vec3(1,0,0); v4sf viewStepX = vViewStepX;
    vec3 vViewStepY = projection.world * vec3(0,1,0); v4sf viewStepY = vViewStepY;
    const v4sf worldOrigin = projection.world * vec3(0,0,0) - (float(imageX-1)/2)*vViewStepX - (float(imageY-1)/2)*vViewStepY;

    parallel(imageX/tileSize*imageY/tileSize, [&](uint, uint i) {
        const int tileX = i%(imageX/tileSize), tileY = i/(imageX/tileSize);
        float* const image = imageData+tileY*tileSize*imageStride+tileX*tileSize;
        const v4sf tileOrigin = worldOrigin + float4(tileX * tileSize) * viewStepX + float4(tileY * tileSize) * viewStepY;
        for(uint y=0; y<tileSize; y++) for(uint x=0; x<tileSize; x++) {
            const v4sf origin = tileOrigin + float4(x) * viewStepX + float4(y) * viewStepY;
            image[y*imageStride+x] = source.project(projection, origin);
        }
    } );
}

/// Computes residual r = p = At ( b - A x )
void CGNR::initialize(const ref<Projection>& projections, const ref<ImageF>& images) {
    int2 imageSize = images.first().size();
    zOrder2 = buffer<v2hi>(imageSize.y * imageSize.x); // Large lookup table (~512K) but read sequentially (stream read, skip cache)
    for(uint index: range(zOrder2.size)) zOrder2[index] = short2(::zOrder2(index)); // Generates lookup table (assumes square POT images)

    assert(projections.size == images.size);
    CylinderVolume volume (x);
    float* pData = (float*)p.data.data;
    float* rData = (float*)r.data.data;
    const v2hi* zOrder2Data = zOrder2.data;
    for(uint projectionIndex: range(projections.size)) { // TODO: Cluster rays for coherence (even from different projections)
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        // Pixel-driven
        float* const imageData = (float*)image.data;
        vec3 vViewStepX = projection.world * vec3(1,0,0); v4sf viewStepX = vViewStepX;
        vec3 vViewStepY = projection.world * vec3(0,1,0); v4sf viewStepY = vViewStepY;
        const v4sf worldOrigin = projection.world * vec3(0,0,0) - (float(image.size().x-1)/2)*vViewStepX - (float(image.size().y-1)/2)*vViewStepY;

        parallel(image.data.size, [&](uint, uint pixelIndex) { // Process pixels in zOrder for coherence (TODO: 2x2x2 ray packets)
            v2hi pixelPosition = zOrder2Data[pixelIndex];
            const v4sf origin = worldOrigin + float4(pixelPosition[0]) * viewStepX + float4(pixelPosition[1]) * viewStepY;
            float b = imageData[pixelPosition[1]*image.width+pixelPosition[0]];
            v8sf pStart; v4sf tEnd;
            float length = volume.intersect(projection, origin, pStart, tEnd);
            if(length) {
                float Ax = volume.project(projection, pStart, tEnd);
                float p = (b - Ax) / length;
                volume.backproject(pData, projection, pStart, tEnd, p);
            }
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
        // Pixel-driven
        vec3 vViewStepX = projection.world * vec3(1,0,0); v4sf viewStepX = vViewStepX;
        vec3 vViewStepY = projection.world * vec3(0,1,0); v4sf viewStepY = vViewStepY;
        const v4sf worldOrigin = projection.world * vec3(0,0,0) - (float(image.size().x-1)/2)*vViewStepX - (float(image.size().y-1)/2)*vViewStepY;

        parallel(image.data.size, [&](uint, uint pixelIndex) { // Process pixels in zOrder for coherence (TODO: 2x2x2 ray packets)
            v2hi pixelPosition = zOrder2Data[pixelIndex];
            const v4sf origin = worldOrigin + float4(pixelPosition[0]) * viewStepX + float4(pixelPosition[1]) * viewStepY;
            v8sf pStart; v4sf tEnd;
            float length = volume.intersect(projection, origin, pStart, tEnd);
            if(length) {
                float Ap = volume.project(projection, pStart, tEnd);
                volume.backproject(AtApData, projection, pStart, tEnd, Ap / length); // AtAp
            }
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
