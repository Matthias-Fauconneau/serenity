#include "project.h"
#include "thread.h"

// SIMD constants for cylinder intersections
static const v4df _2f = double4( 2 );
#define FLT_MAX __FLT_MAX__
static const v4df mrealMax = double4(-FLT_MAX);
static const v4df realMax = double4(FLT_MAX);
static const v4df signPPNN = (v4df)(v8si){0,0,0,0,(int)0x80000000,0,(int)0x80000000,0};
static const v4df realMMMm = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX};

struct CylinderVolume {
    const VolumeP& volume;
    // Precomputed parameters
    int3 size = volume.sampleCount;
    const real radius = size.x/2-1, halfHeight = size.z/2-1; // Cylinder parameters
    const v4df capZ = {halfHeight, halfHeight, -halfHeight, -halfHeight};
    const v4df radiusSqHeight = {radius*radius, radius*radius, halfHeight, halfHeight};
    const v4df radiusR0R0 = {radius*radius, 0, radius*radius, 0};
    const v4df volumeOrigin = {(real)(size.x-1)/2, (real)(size.y-1)/2, (real)(size.z-1)/2, 0};
    P* const volumeData = volume;
    const uint64* const offsetX = volume.offsetX.data;
    const uint64* const offsetY = volume.offsetY.data;
    const uint64* const offsetZ = volume.offsetZ.data;

    CylinderVolume(const VolumeP& volume) : volume(volume) { assert_(volume.tiled() && size.x == size.y); }
    real accumulate(const Projection& p, const v4df origin, real& length);
};

real CylinderVolume::accumulate(const Projection& p, const v4df origin, real& length) {
    // Intersects cap disks
    const v4df originZ = shuffle(origin, origin, 2,2,2,2);
    const v4df capT = (capZ - originZ) * p.raySlopeZ; // top top bottom bottom
    const v4df originXYXY = shuffle(origin, origin, 0,1,0,1);
    const v4df capXY = originXYXY + capT * p.rayXYXY;
    const v4df capR = dot2(capXY, capXY); // top bottom top bottom
    // Intersect cylinder side
    const v4df originXYrayXY = shuffle(origin, p.ray, 0,1,0,1); // Ox Oy Dx Dy
    const v4df cbcb = dot2(originXYXY, originXYrayXY); // OO OD OO OD (b=2OD)
    //const v4df _1b1b = blendpd(_1d, cbcb, 0b1010); // 1 OD 1 OD
    const v4df _1b1b = {_1d[0], cbcb[1], _1d[2], cbcb[3]}; // 1 OD 1 OD
    const v4df _4ac_bb = p._m4a_4_m4a_4 * (cbcb*_1b1b - radiusR0R0); // -4ac bb -4ac bb
    v4df delta = hadd(_4ac_bb,_4ac_bb);
    //const v4df sqrtDelta = sqrt( delta );
    const v4df sqrtDelta = blendv(sqrt(max(_0d,delta)), double4(nan), delta<_0d); // Same as sqrt(delta) without invalid exception
    const v4df sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
    const v4df sideT = (_2f*_1b1b + sqrtDeltaPPNN) * p.rcp_2a; // ? t+ ? t-
    const v4df sideZ = abs(originZ + sideT * p.rayZ); // ? z+ ? z-
    const v4df capSideP = shuffle(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
    //const v4df tMask = capSideP < radiusSqHeight;
    const v4df tMask = blendv(capSideP, radiusSqHeight, delta<_0d) < radiusSqHeight; // Same without invalid exception
    if(!mask(tMask)) { length=0; return 0; } // 26 instructions

    // 11 instructions
    const v4df capSideT = shuffle(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
    const v4df tmin = hmin( blendv(realMax, capSideT, tMask) );
    const v4df tmax = hmax( blendv(mrealMax, capSideT, tMask) );
    length = tmax[0]-tmin[0]; // Ray length (in steps)
    v4df position = volumeOrigin + origin + tmin * p.ray;
    const v4df texit = max(realMMMm, tmax); // max, max, max, tmax
    real sum = 0; // Accumulates/Updates samples along the ray
    do { // 24 instructions
        // Lookups sample offsets
        const v4si p0 = cvttpd2dq(position);
        assert(p0[0]>=0 && p0[0]<size[0] && p0[1]>=0 && p0[1]<size[1] && p0[2]>=0 && p0[2]<size[2], p0[0], p0[1], p0[2]);
        const uint vx0 = offsetX[p0[0]], vy0 = offsetY[p0[1]], vz0 = offsetZ[p0[2]]; //FIXME: gather
        const v4si p1 = p0 +_1i;
        assert(p1[0]>=0 && p1[0]<size[0] && p1[1]>=0 && p1[1]<size[1] && p1[2]>=0 && p1[2]<size[2], p1[0], p1[1], p1[2]);
        const uint vx1 = offsetX[p1[0]], vy1 = offsetY[p1[1]], vz1 = offsetZ[p1[2]]; //FIXME: gather
        // Loads samples (FIXME: gather)
        const v4df cx0 = {volumeData[vx0 + vy0 + vz0], volumeData[vx0 + vy0 + vz1], volumeData[vx0 + vy1 + vz0], volumeData[vx0 + vy1 + vz1]};
        const v4df cx1 = {volumeData[vx1 + vy0 + vz0], volumeData[vx1 + vy0 + vz1], volumeData[vx1 + vy1 + vz0], volumeData[vx1 + vy1 + vz1]};
        // Computes trilinear interpolation coefficients
        const v4df pc = position - cvtdq2pd(p0);
        const v4df _1mpc = _1d - pc;
        const v4df x1111 = shuffle(pc, pc, 0,0,0,0);
        const v4df z0011 = shuffle(_1mpc, pc, 2,2,2,2);
        const v4df y0011 = shuffle(_1mpc, pc, 1,1,1,1);
        const v4df x0000 = shuffle(_1mpc, _1mpc, 0,0,0,0);
        const v4df z0101 = shuffle(z0011, z0011, 0,2,0,2);
        const v4df sw_yz = z0101 * y0011;
        const v2df dpfv = dot4(sw_yz, x0000*cx0 + x1111*cx1);
        sum += dpfv[0]; // Accumulates trilinearly interpolated sample
        position = position + p.ray; // Step
    } while(!mask(position > texit)); // Check for exit intersection
    return sum;
}

void project(const ImageP& image, const VolumeP& volume, Projection projection) {
    CylinderVolume source(volume);

    // Image
    constexpr uint tileSize = 8;
    int imageX = floor(tileSize,image.width), imageY = floor(tileSize,image.height), imageStride = image.width; // Target image size
    assert_(imageX%tileSize ==0 && imageY%tileSize ==0);
    P* const imageData = (P*)image.data;
    vec3 vViewStepX = projection.world * vec3(1,0,0); v4df viewStepX = vViewStepX;
    vec3 vViewStepY = projection.world * vec3(0,1,0); v4df viewStepY = vViewStepY;
    const v4df worldOrigin = v4df(projection.world * vec3(0,0,0)) - double4(imageX/2)*viewStepX - double4(imageY/2)*viewStepY;

    parallel(imageX/tileSize*imageY/tileSize, [&](uint, uint i) {
        const int tileX = i%(imageX/tileSize), tileY = i/(imageX/tileSize);
        P* const image = imageData+tileY*tileSize*imageStride+tileX*tileSize;
        const v4df tileOrigin = worldOrigin + double4(tileX * tileSize) * viewStepX + double4(tileY * tileSize) * viewStepY;
        for(uint y=0; y<tileSize; y++) for(uint x=0; x<tileSize; x++) {
            const v4df origin = tileOrigin + double4(x) * viewStepX + double4(y) * viewStepY;
            real length;
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
    const real radiusSq = sq(center.x);
    CylinderVolume volume (p);
    const vec2 imageCenter = vec2(images.first().size())/2.f;
    const P* pData = (P*)p.data.data;
    P* xData = (P*)x.data.data;
    parallel(p.size(), [&](uint, uint index) {
        const vec3 origin = vec3(zOrder(index)) - center;
        if(sq(origin.xy()) > radiusSq) return;
        real projectionSum = 0, lengthSum = 0;
        real reconstructionSum = 0, pointCount = 0;
        // Projects/Accumulates: At A x
        for(uint projectionIndex: range(projections.size)) {
            const Projection& projection = projections[projectionIndex];
            real length;
            real sum = volume.accumulate(projection, origin, length);
            reconstructionSum += sum;
            lengthSum += length;
            pointCount += floor(length);
        }
        // Samples: At b
        for(uint projectionIndex: range(projections.size)) {
            const Projection& projection = projections[projectionIndex];
            const ImageF& P = images[projectionIndex];
            vec2 xy = (projection.projection * origin).xy() + imageCenter;
            uint i = xy.x, j = xy.y;
            real u = fract(xy.x), v = fract(xy.y);
            real s = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                         v  * ((1-u) * P(i,j+1) + u * P(i+1,j+1)) ;
            projectionSum += s;
        }
        // Updates: x[k+1]|v = max(0, x[k]|v + At b - At A x)
        xData[index] = max(0.f, pData[index] + projectionSum/lengthSum - reconstructionSum/pointCount);
    });
}
#endif

/// Computes residual p = r = At b - At A x
void CGNR::initialize(const ref<Projection>& projections, const ref<ImageP>& images) {
    residual(projections, images, p);
}

/// Computes residual r = At b - At A x (r is the negative gradient of A at x)
void CGNR::residual(const ref<Projection>& projections, const ref<ImageP>& images, const VolumeP& additionalTarget) {
    assert(projections.size == images.size);
    const vec3 center = vec3(r.sampleCount-int3(1))/2.f;
    const real radiusSq = sq(center.x);
    CylinderVolume volume (x);
    const vec2 imageCenter = vec2(images.first().size()-int2(1))/2.f;
    P* pData = (P*)additionalTarget.data.data; // p | r (single target)
    P* rData = (P*)r.data.data;
    real residualSum[coreCount] = {};
    parallel(r.size(), [&](uint id, uint index) {
        const vec3 origin = vec3(zOrder(index)) - center;
        if(sq(origin.xy()) >= radiusSq) return;
        real projectionSum = 0, lengthSum = 0;
        real reconstructionSum = 0, pointCount = 0;
        // Projects/Accumulates: At A x
        for(uint projectionIndex: range(projections.size)) {
            const Projection& projection = projections[projectionIndex];
            real length;
            real sum = volume.accumulate(projection, origin, length);
            reconstructionSum += sum;
            lengthSum += length;
            pointCount += floor(length);
        }
        // Samples: At b
        for(uint projectionIndex: range(projections.size)) {
            const Projection& projection = projections[projectionIndex];
            const ImageP& P = images[projectionIndex];
            vec2 xy = (projection.projection * origin).xy() + imageCenter;
            uint i = xy.x, j = xy.y;
            real u = fract(xy.x), v = fract(xy.y);
#if 0
            real s = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                         v  * ((1-u) * P(i,j+1) + u * P(i+1,j+1));
#else // DEBUG: without underflow
            real s;
            /**/ if(v<=0x1p-128) {
                /**/ if(u<=0x1p-128) s = P(i,j);
                else if(1-u<=0x1p-128) s = P(i+1, j);
                else {
                    assert(i+1<P.width, i,j, u,v);
                    s = (1-u) * P(i, j) + u * P(i+1, j);
                }
            }
            else if(1-v<=0x1p-128) {
                /**/ if(u<=0x1p-128) s = P(i, j+1);
                else if(1-u<=0x1p-128) s = P(i+1, j+1);
                else s = (1-u) * P(i, j+1) + u * P(i+1, j+1);
            }
            else {
                assert(i+1<P.width, i,j, u,v);
                s = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                        v  * ((1-u) * P(i,j+1) + u * P(i+1,j+1));
            }
#endif
            projectionSum += s;
        }
        // Updates: p[k]|v = r[k]|v = At b - At A x
        if(lengthSum && pointCount)
            pData[index] = rData[index] = projectionSum/lengthSum - reconstructionSum/pointCount; // Same as SIRT without constraint
        residualSum[id] += sq(rData[index]);
    });
    residualEnergy = sum(residualSum);
}

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + At α p[k]
void CGNR::step(const ref<Projection>& projections, const ref<ImageP>& /*images*/) {
    const vec3 center = vec3(p.sampleCount)/2.f;
    const real radiusSq = sq(center.x);
    CylinderVolume volume (p);
    P* pData = (P*)p.data.data; // p
    P* AtApData = (P*)AtAp.data.data; // At A p
    real pAtAp[coreCount] = {};
    parallel(p.size(), [&](uint id, uint index) {
        const vec3 origin = vec3(zOrder(index)) - center;
        if(sq(origin.xy()) > radiusSq) return;
        real reconstructionSum = 0, pointCount = 0;
        // Projects/Accumulates: At A p
        for(uint projectionIndex: range(projections.size)) { // At
            const Projection& projection = projections[projectionIndex];
            real length;
            real sum = volume.accumulate(projection, origin, length); // A p
            reconstructionSum += sum;
            pointCount += floor(length);
        }
        AtApData[index] = pointCount ? reconstructionSum/pointCount : 0; // At A p
        pAtAp[id] += pData[index] * AtApData[index]; // p · At A p
    });
    real alpha = residualEnergy / sum(pAtAp);
    P* xData = (P*)x.data.data; // x
    P* rData = (P*)r.data.data; // r
    real newResidualSum[coreCount] = {};
    chunk_parallel(p.size(), [&](uint id, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            xData[index] += alpha * pData[index];
            rData[index] -= alpha * AtApData[index];
            newResidualSum[id] += sq(rData[index]);
        }
    });
    //residual(projections, images, r);
    real newResidual = sum(newResidualSum);
    real beta = newResidual / residualEnergy;
    // Computes next search direction
    chunk_parallel(p.size(), [&](uint, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            pData[index] = rData[index] + beta * pData[index];
        }
    });
    residualEnergy = newResidual;
}
