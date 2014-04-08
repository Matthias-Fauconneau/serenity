#include "project.h"
#include "thread.h"

// SIMD constants for cylinder intersections
static const v4sf _2f = float4( 2 );
#define FLT_MAX __FLT_MAX__
static const v4sf mfloatMax = float4(-FLT_MAX);
static const v4sf floatMax = float4(FLT_MAX);
static const v4sf signPPNN = (v4sf)(v4si){0,0,(int)0x80000000,(int)0x80000000};
static const v4sf floatMMMm = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX};

struct CylinderVolume {
    const VolumeF& volume;
    // Precomputed parameters
    int3 size = volume.sampleCount;
    const float radius = (size.x-1)/2, halfHeight = (size.z-1)/2; // Cylinder parameters
    const v4sf capZ = {halfHeight, halfHeight, -halfHeight, -halfHeight};
    const v4sf radiusSqHeight = {radius*radius, radius*radius, halfHeight, halfHeight};
    const v4sf radiusR0R0 = {radius*radius, 0, radius*radius, 0};
    const v4sf volumeOrigin = {float(size.x-1)/2, float(size.y-1)/2, float(size.z-1)/2, 0};
    float* const volumeData = volume;
    const uint64* const offsetX = volume.offsetX.data;
    const uint64* const offsetY = volume.offsetY.data;
    const uint64* const offsetZ = volume.offsetZ.data;

    CylinderVolume(const VolumeF& volume) : volume(volume) { assert_(volume.tiled() && size.x == size.y); }
    float accumulate(const Projection& p, const v4sf origin, float& length);
};

float CylinderVolume::accumulate(const Projection& p, const v4sf origin, float& length) {
    // Intersects cap disks
    const v4sf originZ = shuffle(origin, origin, 2,2,2,2);
    const v4sf capT = (capZ - originZ) * p.raySlopeZ; // top top bottom bottom
    const v4sf originXYXY = shuffle(origin, origin, 0,1,0,1);
    const v4sf capXY = originXYXY + capT * p.rayXYXY;
    const v4sf capR = dot2(capXY, capXY); // top bottom top bottom
    // Intersect cylinder side
    const v4sf originXYrayXY = shuffle(origin, p.ray, 0,1,0,1); // Ox Oy Dx Dy
    const v4sf cbcb = dot2(originXYXY, originXYrayXY); // OO OD OO OD (b=2OD)
    const v4sf _1b1b = blendps(_1f, cbcb, 0b1010); // 1 OD 1 OD
    const v4sf _4ac_bb = p._m4a_4_m4a_4 * (cbcb*_1b1b - radiusR0R0); // -4ac bb -4ac bb
    v4sf delta = hadd(_4ac_bb,_4ac_bb);
    const v4sf sqrtDelta = sqrt( delta );
    const v4sf sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
    const v4sf sideT = (_2f*_1b1b + sqrtDeltaPPNN) * p.rcp_2a; // ? t+ ? t-
    const v4sf sideZ = abs(originZ + sideT * p.rayZ); // ? z+ ? z-
    const v4sf capSideP = shuffle(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
    const v4sf tMask = capSideP < radiusSqHeight;
    if(!mask(tMask)) { length=0; return 0; } // 26 instructions

    // 11 instructions
    const v4sf capSideT = shuffle(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
    const v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
    const v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
    length = tmax[0]-tmin[0]; // Ray length (in steps)
    v4sf position = volumeOrigin + origin + tmin * p.ray;
    const v4sf texit = max(floatMMMm, tmax); // max, max, max, tmax
    float sum = 0; // Accumulates/Updates samples along the ray
    assert_(!mask(position > texit));
    do { // 24 instructions
        // Lookups sample offsets
        const v4si p0 = cvttps2dq(position);
        assert_(p0[0]>=0 && p0[0]<size[0] && p0[1]>=0 && p0[1]<size[1] && p0[2]>=0 && p0[2]<size[2], p0[0], p0[1], p0[2]);
        const uint vx0 = offsetX[p0[0]], vy0 = offsetY[p0[1]], vz0 = offsetZ[p0[2]]; //FIXME: gather
        const v4si p1 = p0 +_1i;
        assert_(p1[0]>=0 && p1[0]<size[0] && p1[1]>=0 && p1[1]<size[1] && p1[2]>=0 && p1[2]<size[2], p1[0], p1[1], p1[2]);
        const uint vx1 = offsetX[p1[0]], vy1 = offsetY[p1[1]], vz1 = offsetZ[p1[2]]; //FIXME: gather
        // Loads samples (FIXME: gather)
        const v4sf cx0 = {volumeData[vx0 + vy0 + vz0], volumeData[vx0 + vy0 + vz1], volumeData[vx0 + vy1 + vz0], volumeData[vx0 + vy1 + vz1]};
        const v4sf cx1 = {volumeData[vx1 + vy0 + vz0], volumeData[vx1 + vy0 + vz1], volumeData[vx1 + vy1 + vz0], volumeData[vx1 + vy1 + vz1]};
        // Computes trilinear interpolation coefficients
        const v4sf pc = position - cvtdq2ps(p0);
        const v4sf _1mpc = _1f - pc;
        const v4sf x1111 = shuffle(pc, pc, 0,0,0,0);
        const v4sf z0011 = shuffle(_1mpc, pc, 2,2,2,2);
        const v4sf y0011 = shuffle(_1mpc, pc, 1,1,1,1);
        const v4sf x0000 = shuffle(_1mpc, _1mpc, 0,0,0,0);
        const v4sf z0101 = shuffle(z0011, z0011, 0,2,0,2);
        const v4sf sw_yz = z0101 * y0011;
        const v4sf dpfv = dot4(sw_yz, x0000*cx0 + x1111*cx1);
        assert_(mask(dot4(sw_yz, x0000*_1f + x1111*_1f) == _1f)==0b1111);
        sum += dpfv[0]; // Accumulates trilinearly interpolated sample
        position = position + p.ray; // Step
    } while(!mask(position > texit)); // Check for exit intersection
    return sum;
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
            float length;
            image[y*imageStride+x] = source.accumulate(projection, origin, length);
        }
    } );
}

#if SIRT
/// Minimizes |Ax-b|² using constrained gradient descent: x[k+1] = max(0, x[k] + At b - At A x[k] )
void SIRT::step(const ref<Projection>& projections, const ref<ImageF>& images) {
    swap(p, x);
    assert(x.sampleCount == p.sampleCount && projections.size == images.size);
    const vec3 center = vec3(p.sampleCount)/2.f;
    const float radiusSq = sq(center.x);
    CylinderVolume volume (p);
    const vec2 imageCenter = vec2(images.first().size())/2.f;
    const float* pData = (float*)p.data.data;
    float* xData = (float*)x.data.data;
    parallel(p.size(), [&](uint, uint index) {
        const vec3 origin = vec3(zOrder(index)) - center;
        if(sq(origin.xy()) > radiusSq) return;
        float projectionSum = 0, lengthSum = 0;
        float reconstructionSum = 0, pointCount = 0;
        // Projects/Accumulates: At A x
        for(uint projectionIndex: range(projections.size)) {
            const Projection& projection = projections[projectionIndex];
            float length;
            float sum = volume.accumulate(projection, origin, length);
            reconstructionSum += sum;
            lengthSum += length;
            pointCount += ceil(length);
        }
        // Samples: At b
        for(uint projectionIndex: range(projections.size)) {
            const Projection& projection = projections[projectionIndex];
            const ImageF& P = images[projectionIndex];
            vec2 xy = (projection.projection * origin).xy() + imageCenter;
            uint i = xy.x, j = xy.y;
            float u = fract(xy.x), v = fract(xy.y);
            float s = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                         v  * ((1-u) * P(i,j+1) + u * P(i+1,j+1)) ;
            projectionSum += s;
        }
        // Updates: x[k+1]|v = max(0, x[k]|v + At b - At A x)
        xData[index] = max(0.f, pData[index] + projectionSum/lengthSum - reconstructionSum/pointCount);
    });
}
#endif

/// Computes residual p = r = At b - At A x
void CGNR::initialize(const ref<Projection>& projections, const ref<ImageF>& images) {
    residualEnergy = residual(projections, images, r, p);
}

/// Computes residual r = At b - At A x (r is the negative gradient of A at x)
float CGNR::residual(const ref<Projection>& projections, const ref<ImageF>& images, const VolumeF& a, const VolumeF& b) {
    assert(projections.size == images.size);
    const vec3 center = vec3(r.sampleCount-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    CylinderVolume volume (x);
    const vec2 imageCenter = vec2(images.first().size()-int2(1))/2.f;
    float* aData = (float*)a.data.data; // p | r (single target)
    float* bData = (float*)b.data.data;
    float residualSum[coreCount] = {};
    parallel(r.size(), [&](uint id, uint index) {
        const vec3 origin = vec3(zOrder(index)) - center;
        if(sq(origin.xy()) > radiusSq) return;
        float projectionSum = 0, lengthSum = 0;
        float reconstructionSum = 0, pointCount = 0;
        // Projects/Accumulates: At A x
        for(uint projectionIndex: range(projections.size)) {
            const Projection& projection = projections[projectionIndex];
            float length;
            float sum = volume.accumulate(projection, origin, length);
            reconstructionSum += sum;
            lengthSum += length;
            pointCount += ceil(length);
        }
        // Samples: At b
        for(uint projectionIndex: range(projections.size)) {
            const Projection& projection = projections[projectionIndex];
            const ImageF& P = images[projectionIndex];
            vec2 xy = (projection.projection * origin).xy() + imageCenter;
            uint i = xy.x, j = xy.y;
            float u = fract(xy.x), v = fract(xy.y);
            float s = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                         v  * ((1-u) * P(i,j+1) + u * P(i+1,j+1));
            projectionSum += s;
        }
        // Updates: p[k]|v = r[k]|v = At b - At A x
        if(lengthSum && pointCount) {
            float r = projectionSum/lengthSum - reconstructionSum/pointCount; // Same as SIRT without constraint
            aData[index] = r, bData[index] = r;
            residualSum[id] += sq(r);
        }
    });
    return sum(residualSum);
}

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + At α p[k]
bool CGNR::step(const ref<Projection>& projections, const ref<ImageF>&) {
    k++;
    const vec3 center = vec3(p.sampleCount-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    CylinderVolume volume (p);
    float* pData = (float*)p.data.data; // p
    float* AtApData = (float*)AtAp.data.data; // At A p
    float pAtAp[coreCount] = {};
    parallel(p.size(), [&](uint id, uint index) {
        const vec3 origin = vec3(zOrder(index)) - center;
        if(sq(origin.xy()) > radiusSq) return;
        float reconstructionSum = 0, pointCount = 0;
        // Projects/Accumulates: At A p
        for(uint projectionIndex: range(projections.size)) { // At
            const Projection& projection = projections[projectionIndex];
            float length;
            float sum = volume.accumulate(projection, origin, length); // A p
            reconstructionSum += sum;
            pointCount += floor(length);
        }
        AtApData[index] = pointCount ? reconstructionSum/pointCount : 0; // At A p
        pAtAp[id] += pData[index] * AtApData[index]; // p · At A p
    });
    float alpha = residualEnergy / sum(pAtAp);
    float* xData = (float*)x.data.data; // x
    float* rData = (float*)r.data.data; // r
    float estimateSum[coreCount] = {};
    float newResidualSum[coreCount] = {};
    chunk_parallel(p.size(), [&](uint id, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            xData[index] += alpha * pData[index];
            estimateSum[id] += sq(xData[index]);
            rData[index] -= alpha * AtApData[index];
            newResidualSum[id] += sq(rData[index]);
            const vec3 origin = vec3(zOrder(index)) - center;
            if(sq(origin.xy()) > radiusSq) assert_(xData[index]==0, index, zOrder(index), origin, xData[index], pData[index], rData[index]);
        }
    });
    float newResidual = sum(newResidualSum);
    float beta = newResidual / residualEnergy;
    float estimateEnergy = sum(estimateSum);
    log(k, residualEnergy, newResidual, beta, estimateEnergy, newResidual/estimateEnergy);
    if(beta > 1) return false;
    // Computes next search direction
    chunk_parallel(p.size(), [&](uint, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            pData[index] = rData[index] + beta * pData[index];
        }
    });
    residualEnergy = newResidual;
    return true;
}
