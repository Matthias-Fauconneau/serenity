#include "project.h"
#include "thread.h"

// SIMD constants for intersections
#define FLT_MAX __FLT_MAX__
static const v4sf _2f = float4( 2 );
static const v4sf mfloatMax = float4(-FLT_MAX);
static const v4sf floatMax = float4(FLT_MAX);
static const v4sf signPPNN = (v4sf)(v4si){0,0,(int)0x80000000,(int)0x80000000};
static const v4sf floatMMMm = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX};

struct CylinderVolume {
    CylinderVolume(const VolumeF& volume) {
        assert(volume.sampleCount.x == volume.sampleCount.y);
        const float radius = (volume.sampleCount.x-1)/2, halfHeight = (volume.sampleCount.z-1)/2; // Cylinder parameters (N-1 [domain size] - epsilon (Prevents out of bounds on exact $-1 (ALT: extend offsetZ by one row (gather anything and multiply by 0))
        capZ = (v4sf){halfHeight, halfHeight, -halfHeight, -halfHeight};
        radiusR0R0 = (v4sf){radius*radius, 0, radius*radius, 0};
        radiusSqHeight = (v4sf){radius*radius, radius*radius, halfHeight, halfHeight};
        dataOrigin = float4(float(volume.sampleCount.x-1)/2, float(volume.sampleCount.y-1)/2, float(volume.sampleCount.z-1)/2, 0);
        stride = (v4si){1, volume.sampleCount.x,volume.sampleCount.x*volume.sampleCount.y, 0};
        offset = (v4si){volume.sampleCount.x,volume.sampleCount.x*volume.sampleCount.y,volume.sampleCount.x,volume.sampleCount.x*volume.sampleCount.y};
        data = volume;
    }

    // Precomputed parameters
    v4sf capZ;
    v4sf radiusR0R0;
    v4sf radiusSqHeight;
    v4sf dataOrigin;
    v4si stride;
    v4si offset;
    float* data;
};

/// Integrates \a volume along ray defined by (\a projection, \a pixelPosition). if \a imageData and not \a targetData: stores result into image; if targetData: backprojects value; if imageData and targetData: backprojects difference
/// \note Splitting intersect / projects / updates / backprojects triggers spurious uninitialized warnings when there is no intersection test, which suggests the compiler is not able to flatten the code.
static inline float update(const Projection& projection, vec2 pixelPosition, const CylinderVolume& volume, float b=0, float* targetData=0) {
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
    const v4sf tMask = capSideP < volume.radiusSqHeight;
    if(!mask(tMask)) return 0;
    const v4sf capSideT = shuffle4(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
    v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
    v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
    v4sf pStart = volume.dataOrigin + origin + tmin * projection.ray;
    v4sf tEnd = max(floatMMMm, tmax); // max, max, max, tmax
    /// Projects
    // TODO: check code
    v4sf accumulator = _0f; // Accumulates along the ray
    for(v4sf p01f = pStart, step=projection.ray;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
        const v4si p01 = cvttps2dq(p01f); // Converts position to integer coordinates
        const v4si index = dot4(p01, volume.stride); // to voxel index
        const v4si v01 = index + volume.offset; // to 4 voxel indices
        const v8sf x01 = gather2(volume.data, v01); // Gather samples
        const v4sf fract = p01f - cvtdq2ps(p01);
        const v8sf w_1mw = merge(fract, _1f-fract); // Computes trilinear interpolation coefficients
        const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
        accumulator += dot8(w01, x01); // Accumulates trilinearly interpolated sample
        p01f += step; // Step
        if(mask(p01f > tEnd)) break;
    }
    float Ax = accumulator[0];
    if(!targetData) return Ax; // Projection only
    Ax -= b;  //+ regularization(x, index); // = 0
    /// Backprojects
    v8sf value8 = float8(Ax);
    for(v4sf p01f = pStart, step=projection.ray;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
        const v4si p01 = cvttps2dq(p01f); // Converts position to integer coordinates
        const v4si index = dot4(p01, volume.stride); // to voxel index
        const v4si v01 = index + volume.offset; // to 4 voxel indices
        const v8sf x01 = gather2(volume.data, v01); // Gather samples
        const v4sf fract = p01f - cvtdq2ps(p01);
        const v8sf w_1mw = merge(fract, _1f-fract); // Computes trilinear interpolation coefficients
        const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
        const v8sf y01 = x01 + w01 * value8;
        (v2sf&)(targetData[v01[0]]) = __builtin_shufflevector(y01, y01, 0, 1);
        (v2sf&)(targetData[v01[1]]) = __builtin_shufflevector(y01, y01, 2, 3);
        (v2sf&)(targetData[v01[2]]) = __builtin_shufflevector(y01, y01, 4, 5);
        (v2sf&)(targetData[v01[3]]) = __builtin_shufflevector(y01, y01, 6, 7);
        p01f += step;
        if(mask(p01f > tEnd)) break;
    }
    return Ax;
}

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    const CylinderVolume volume(source);
    float* const imageData = image.data;
    uint imageWidth = image.width;
    parallel(image.data.size, [&projection, &volume, imageData, imageWidth](uint, uint index) { uint x=index%imageWidth, y=index/imageWidth; imageData[y*imageWidth+x] = update(projection, vec2(x, y), volume); }, coreCount);
}

/// Computes residual r = p = At ( b - A x )
void CGNR::initialize(const ref<Projection>& projections, const ref<ImageF>& images) {
#if !APPROXIMATE
    const CylinderVolume volume (x);
    for(uint projectionIndex: range(projections.size)) { // TODO: Cluster rays for coherence (even from different projections)
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        float* const imageData = image.data;
        uint imageWidth = image.width;
        parallel(image.data.size, [&projection, &volume, imageData, imageWidth, this](uint id, uint index) { int x=index%imageWidth, y=index/imageWidth; update(projection, vec2(x, y), volume, imageData[y*imageWidth+x], AtAp[id]); }, coreCount);
    }
#endif
    // Merges r=p=sum(p#) and computes |r|
    float* pData = p;
    float* rData = r;
    float residualSum[coreCount] = {};
#if APPROXIMATE // Approximates trilinear backprojection with bilinear sample (reverse splat)
    const vec3 center = vec3(p.sampleCount-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const vec2 imageCenter = vec2(images.first().size()-int2(1))/2.f;
    chunk_parallel(p.size(), [&projections, &images, center, radiusSq, imageCenter, pData, rData, &residualSum, this](uint id, uint offset, uint size) {
        for(uint i: range(offset,offset+size)) {
            const vec3 origin = vec3(i%x.sampleCount.x, (i/x.sampleCount.x)%x.sampleCount.y, i/(x.sampleCount.x*x.sampleCount.y)) - center;
            if(sq(origin.xy()) > radiusSq) continue;
            float Atb = 0;
            for(uint projectionIndex: range(projections.size)) {
                const Projection& projection = projections[projectionIndex];
                const ImageF& P = images[projectionIndex];
                vec2 xy = (projection.projection * origin).xy() + imageCenter;
                uint i = xy.x, j = xy.y;
                float u = fract(xy.x), v = fract(xy.y);
                float s = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                        v  * ((1-u) * P(i,j+1) + u * P(i+1,j+1)) ;
                Atb += s;
            }
#else
    chunk_parallel(p.size(), [pData, rData, &residualSum, this](uint id, uint offset, uint size) {
        float* P[coreCount]; for(uint id: range(coreCount)) P[id] = AtAp[id];
        for(uint i: range(offset,offset+size)) {
            float Atb = 0;
            for(uint id: range(coreCount)) { Atb -= P[id][i]; /*- as we compute Ax - b instead of b - Ax (to factorize update for step)*/ P[id][i]=0; } // Merges p and clears AtAp#
#endif
            //Atb -= regularization(x, index); // = 0
            pData[i] = Atb;
            rData[i] = Atb;
            residualSum[id] += sq(Atb);
        }
    }, coreCount);
    residualEnergy = sum(residualSum);
}


#define REGULARIZATION 0
#if REGULARIZATION
// Computes regularization: QtQ = lambda · ( I + alpha · CtC )
static float regularization(const VolumeF& volume, const uint index) {
    int3 p = int3(index%volume.sampleCount.x, (index/volume.sampleCount.x)%volume.sampleCount.y, index/(volume.sampleCount.x*volume.sampleCount.y));
    assert_(index == uint(volume.offsetX[p.x]+volume.offsetY[p.y]+volume.offsetZ[p.z]), index, p);
    float Ix = volume.data[index];
    float CtCx = 0;
    for(int z=-1; z<=1; z++) for(int y=-1; y<=1; y++) for(int x=-1; x<=1; x++) {
        if(p+int3(x,y,z) >= int3(0) && p+int3(x,y,z) < volume.sampleCount) {
            float Cx = Ix - volume.data[volume.offsetX[p.x+x]+volume.offsetY[p.y+y]+volume.offsetZ[p.z+z]];
            CtCx += sq(Cx);
        }
    }
    float QtQx = Ix + 1 * CtCx;
    return QtQx;
}
#endif

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
bool CGNR::step(const ref<Projection>& projections, const ref<ImageF>& images) {
    totalTime.start();
    Time time; time.start();

#if APPROXIMATE
    // Projects p
    const CylinderVolume volume (p);
    for(uint projectionIndex: range(projections.size)) {
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        float* const imageData = image.data;
        uint imageWidth = image.width;
        parallel(image.data.size, [&projection, &volume, imageData, imageWidth, this](uint, uint index) { vec2 p(index%imageWidth, index/imageWidth); imageData[index] = update(projection, p, volume); }, coreCount);
    }
#else
    // Computes At A p (i.e projects and backprojects p)
    AtApTime.start();
    const CylinderVolume volume (p);
    for(uint projectionIndex: range(projections.size)) {
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        uint imageWidth = image.width;
        parallel(image.data.size, [this, &volume, &projection, imageWidth](uint id, uint index) { int x=index%imageWidth, y=index/imageWidth; update(projection, vec2(x, y), volume, 0, AtAp[id], profiles[id]); }, coreCount);
    }
    AtApTime.stop();
#endif

    // Merges and clears AtAp and computes |p·Atp|
    mergeTime.start();
    float* AtApData = AtAp[0]; // Merges into first volume
    float* pData = p;
    float pAtApSum[coreCount] = {};
#if APPROXIMATE // Approximates trilinear backprojection with bilinear sample (reverse splat)
    const vec3 center = vec3(p.sampleCount-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const vec2 imageCenter = vec2(images.first().size()-int2(1))/2.f;
    chunk_parallel(p.size(), [&projections, &images, center, radiusSq, imageCenter, pData, AtApData, &pAtApSum, this](uint id, uint offset, uint size) {
        for(uint i: range(offset,offset+size)) {
            float AtAp = 0;
            const vec3 origin = vec3(i%x.sampleCount.x, (i/x.sampleCount.x)%x.sampleCount.y, i/(x.sampleCount.x*x.sampleCount.y)) - center;
            if(sq(origin.xy()) > radiusSq) continue;
            for(uint projectionIndex: range(projections.size)) {
                const Projection& projection = projections[projectionIndex];
                const ImageF& P = images[projectionIndex];
                vec2 xy = (projection.projection * origin).xy() + imageCenter;
                uint i = xy.x, j = xy.y;
                float u = fract(xy.x), v = fract(xy.y);
                float s = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                        v  * ((1-u) * P(i,j+1) + u * P(i+1,j+1)) ;
                AtAp += s;
            }
#else
    chunk_parallel(p.size(), [pData, AtApData, &pAtApSum, this](uint id, uint offset, uint size) {
        float* P[coreCount]; for(uint id: range(coreCount)) P[id] = AtAp[id];
        for(uint i: range(offset,offset+size)) {
            float AtAp = 0;
            for(uint id: range(coreCount)) { AtAp += P[id][i]; P[id][i]=0; } // Merges and clears AtAp#
#endif
#if REGULARIZATION
            AtAp += regularization(p, i);
#endif
            AtApData[i] = AtAp;
            pAtApSum[id] += pData[i] * AtAp;
        }
    }, coreCount);
    float pAtAp = sum(pAtApSum);
    mergeTime.stop();

    // Updates x += α p, r -= α AtAp, clears AtAp, computes |r|²
    updateTime.start();
    float alpha = residualEnergy / pAtAp;
    float* xData = x;
    float* rData = r;
    float newResidualSum[coreCount] = {};
    chunk_parallel(p.size(), [alpha, pData, xData, AtApData, rData, &newResidualSum](uint id, uint offset, uint size) {
        for(uint i: range(offset,offset+size)) {
            xData[i] += alpha * pData[i];
            rData[i] -= alpha * AtApData[i];
            AtApData[i] = 0;
            newResidualSum[id] += sq(rData[i]);
        }
    }, coreCount);
    float newResidual = sum(newResidualSum);
    float beta = newResidual / residualEnergy;
    updateTime.stop();

    // Computes next search direction: p[k+1] = r[k+1] + β p[k]
    nextTime.start();
    chunk_parallel(p.size(), [&](uint, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            pData[index] = rData[index] + beta * pData[index];
        }
    }, coreCount);
    nextTime.stop();

    time.stop();
    totalTime.stop();
    k++;
    log(dec(k,2), time, str(totalTime.toFloat()/k)+"s"_);
    residualEnergy = newResidual;
    return true;
}
