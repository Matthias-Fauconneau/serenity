#include "project.h"
#include "thread.h"

// SIMD constants for intersections
#define FLT_MAX __FLT_MAX__
static const v4sf _2f = float4( 2 );
static const v4sf mfloatMax = float4(-FLT_MAX);
static const v4sf floatMax = float4(FLT_MAX);
static const v4sf signPPNN = (v4sf)(v4si){0,0,(int)0x80000000,(int)0x80000000};
static const v4sf floatMMMm = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX};
static const v8si _11101110 = (v8si){~0ll,~0ll,~0ll,0ll, ~0ll,~0ll,~0ll,0ll};
static const v8sf _00001111f = (v8sf){0,0,0,0,1,1,1,1};

struct CylinderVolume {
    CylinderVolume(const VolumeF& volume) {
        assert_(volume.tiled() && volume.sampleCount.x == volume.sampleCount.y, volume.tiled(), volume.sampleCount);
        const float radius = (volume.sampleCount.x-1)/2, halfHeight = (volume.sampleCount.z-1)/2; // Cylinder parameters (N-1 [domain size] - epsilon (Prevents out of bounds on exact $-1 (ALT: extend offsetZ by one row (gather anything and multiply by 0))
        capZ = (v4sf){halfHeight, halfHeight, -halfHeight, -halfHeight};
        radiusR0R0 = (v4sf){radius*radius, 0, radius*radius, 0};
        radiusSqHeight = (v4sf){radius*radius, radius*radius, halfHeight, halfHeight};
        dataOrigin = dup(float4(float(volume.sampleCount.x-1)/2, float(volume.sampleCount.y-1)/2 + (volume.offsetY.data-volume.offsetX.data), float(volume.sampleCount.z-1)/2 + (volume.offsetZ.data-volume.offsetX.data), 0)) + float8(0,0,0,0,1,1,1,0);
        zOrder = volume.offsetX.data;
        data = volume;
    }

    // Precomputed parameters
    v4sf capZ;
    v4sf radiusR0R0;
    v4sf radiusSqHeight;
    v8sf dataOrigin;
    const int32* zOrder;
    float* data;
};

struct Profile { tsc gatherTime, scatterTime, lookupTime, dataTime; } profile[threadCount], discard;

/// Integrates \a volume along ray defined by (\a projection, \a pixelPosition). if \a imageData and not \a targetData: stores result into image; if targetData: backprojects value; if imageData and targetData: backprojects difference
/// \note Splitting intersect / projects / updates / backprojects triggers spurious uninitialized warnings when there is no intersection test, which suggests the compiler is not able to flatten the code.
static inline float update(const Projection& projection, v2hi pixelPosition, const CylinderVolume& volume, float* imageData, const uint imageWidth, float* targetData=0, Profile& profile=discard) {
    /// Intersects
    v4sf origin = projection.origin + float4(pixelPosition[0]) * projection.xAxis + float4(pixelPosition[1]) * projection.yAxis;
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
    if(!mask(tMask)) { if(!targetData) imageData[pixelPosition[1]*imageWidth+pixelPosition[0]]=0; return 0; }
    const v4sf capSideT = shuffle4(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
    v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
    v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
    origin += tmin * projection.ray;
    v8sf pStart = volume.dataOrigin + dup(origin);
    v4sf tEnd = max(floatMMMm, tmax); // max, max, max, tmax
    /// Projects
    profile.gatherTime.start();
    float Ax = 0; // Accumulates along the ray
    for(v8sf p01f = pStart;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
        // Converts {position, position+1} to integer coordinates
        const v8si p01 = cvttps2dq(p01f);
        // Lookups sample offsets
        //if(id>=0) lookupTime[id].start();
        const v8si offsetXYZXYZ = gather(volume.zOrder, p01, _11101110);
        //if(id>=0) lookupTime[id].stop();
        const v8si v01 = shuffle8(offsetXYZXYZ,offsetXYZXYZ, 0,0,0,0, 4,4,4,4) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 1,1, 5,5, 1,1, 5,5) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 2,6, 2,6, 2,6, 2,6);
        // Gather samples
        //if(id>=0) dataTime[id].start();
        const v8sf cx01 = gather(volume.data, v01);
        //if(id>=0) dataTime[id].stop();
        // Computes trilinear interpolation coefficients
        const v8sf w_1mw = abs(p01f - cvtdq2ps(p01) - _00001111f); // fract(x), 1-fract(x)
        const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
        const float value = dot8(w01, cx01);
        Ax += value; // Accumulates trilinearly interpolated sample
        p01f += projection.ray8; // Step
        if(mask(low(p01f) > tEnd)) break;
    }
    profile.gatherTime.stop();
    /// Updates
    if(!targetData) { imageData[pixelPosition[1]*imageWidth+pixelPosition[0]]=Ax; return Ax; }
    float value = imageData ? imageData[pixelPosition[1]*imageWidth+pixelPosition[0]] - Ax : Ax;
    /// Backprojects
    profile.scatterTime.start();
    v8sf value8 = float8(value);
    for(v8sf p01f = pStart;;) { // Uniform ray sampling with trilinear interpolation (24 instructions / step)
        // Converts {position, position+1} to integer coordinates
        const v8si p01 = cvttps2dq(p01f);
        // Lookups sample offsets
        profile.lookupTime.start();
        const v8si offsetXYZXYZ = gather(volume.zOrder, p01, _11101110);
        profile.lookupTime.stop();
        const v8si v01 = shuffle8(offsetXYZXYZ,offsetXYZXYZ, 0,0,0,0, 4,4,4,4) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 1,1, 5,5, 1,1, 5,5) + shuffle8(offsetXYZXYZ,offsetXYZXYZ, 2,6, 2,6, 2,6, 2,6);
        // Gather samples
        profile.dataTime.start();
        v8sf cx01 = gather(targetData, v01);
        profile.dataTime.stop();
        // Computes trilinear interpolation coefficients
        const v8sf w_1mw = abs(p01f - cvtdq2ps(p01) - _00001111f); // fract(x), 1-fract(x)
        const v8sf w01 = shuffle8(w_1mw, w_1mw, 4,4,4,4, 0,0,0,0) * shuffle8(w_1mw, w_1mw, 5,5, 1,1, 5,5, 1,1) * shuffle8(w_1mw, w_1mw, 6,2, 6,2, 6,2, 6,2); // xxxXXXX * yyYYyyYY * zZzZzZzZ = xyz, xyZ, xYz, xYZ, Xyz, XyZ, XYz, XYZ
        // Update
        cx01 += w01 * value8;
        // Scatter ~ scatter(data, v01, cx01);
        targetData[v01[0]] = cx01[0]; targetData[v01[1]] = cx01[1]; targetData[v01[2]] = cx01[2]; targetData[v01[3]] = cx01[3];
        targetData[v01[4]] = cx01[4]; targetData[v01[5]] = cx01[5]; targetData[v01[6]] = cx01[6]; targetData[v01[7]] = cx01[7];
        p01f += projection.ray8; // Step
        if(mask(low(p01f) > tEnd)) break;
    }
    profile.scatterTime.stop();
    return Ax;
}

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    const CylinderVolume volume(source);
    float* const imageData = image.data;
    uint imageWidth = image.width;
    parallel(image.data.size, [&projection, &volume, imageData, imageWidth](uint, uint index) { short2 p = short2(index%imageWidth, index/imageWidth);  update(projection, (v2hi){p.x, p.y}, volume, imageData, imageWidth); }, threadCount);
}

/// Computes residual r = p = At ( b - A x )
void CGNR::initialize(const ref<Projection>& projections, const ref<ImageF>& images) {
    int2 imageSize = images.first().size();
    zOrder2 = buffer<v2hi>(imageSize.y * imageSize.x); // Large lookup table (~512K) but read sequentially (stream read, skip cache)
    for(uint index: range(zOrder2.size)) { short2 p = short2(::zOrder2(index)); zOrder2[index] = (v2hi){p.x, p.y}; } // Generates lookup table (assumes square POT images)
    const v2hi* zOrder2Data = zOrder2.data;
    const CylinderVolume volume (x);
    for(uint projectionIndex: range(projections.size)) { // TODO: Cluster rays for coherence (even from different projections)
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        float* const imageData = image.data;
        uint imageWidth = image.width;
        parallel(image.data.size, [zOrder2Data, &projection, &volume, imageData, imageWidth, this](uint id, uint pixelIndex) { update(projection, zOrder2Data[pixelIndex], volume, imageData, imageWidth, AtAp[id]); }, threadCount); // FIXME: Split rays in coherent non-intersecting chunks
    }
    // Merges r=p=sum(p#) and computes |r|
    float* pData = p;
    float* rData = r;
    float residualSum[coreCount] = {};
    chunk_parallel(p.size(), [pData, rData, &residualSum, this](uint id, uint offset, uint size) {
        float* P[threadCount]; for(uint id: range(threadCount)) P[id] = AtAp[id];
        for(uint i: range(offset,offset+size)) {
            float p = 0;
            for(uint id: range(threadCount)) { p += P[id][i]; P[id][i]=0; } // Merges p and clears AtAp#
            //p -= regularization(x, index); // = 0
            pData[i] = p;
            rData[i] = p;
            residualSum[id] += sq(p);
        }
    }, threadCount);
    residualEnergy = sum(residualSum);
}

/// Minimizes |Ax-b|² using conjugated gradient (on the normal equations): x[k+1] = x[k] + α p[k]
bool CGNR::step(const ref<Projection>& projections, const ref<ImageF>& images) {
    totalTime.start();

    // Computes At A p (i.e projects and backprojects p)
    AtApTime.start();
    const v2hi* zOrder2Data = zOrder2.data;
    const CylinderVolume volume (p);
    for(uint projectionIndex: range(projections.size)) {
        const Projection& projection = projections[projectionIndex];
        const ImageF& image = images[projectionIndex];
        parallel(image.data.size, [zOrder2Data, &projection, &volume, this](uint id, uint pixelIndex) { update(projection, zOrder2Data[pixelIndex], volume, 0, 0, AtAp[id], profile[id]); }, threadCount);
    }
    AtApTime.stop();

    // Merges and clears AtAp and computes |p·Atp|
    mergeTime.start();
    float* AtApData = AtAp[threadCount]; // threadCount+1 volumes, last one is used to merge
    float* pData = p;
    float pAtApSum[coreCount] = {};
    chunk_parallel(p.size(), [pData, AtApData, &pAtApSum, this](uint id, uint offset, uint size) {
        float* P[threadCount]; for(uint id: range(threadCount)) P[id] = AtAp[id];
        for(uint i: range(offset,offset+size)) {
            float AtAp = 0;
            for(uint id: range(threadCount)) { AtAp += P[id][i]; P[id][i]=0; } // Merges and clears AtAp#
            AtApData[i] = AtAp;
            pAtApSum[id] += pData[i] * AtAp;
        }
    }, threadCount);
    float pAtAp = sum(pAtApSum);
    mergeTime.stop();

    // Updates x += α p, r -= α AtAp, clears AtAp, computes |r|²
    updateTime.start();
    float alpha = residualEnergy / pAtAp;
    float* xData = x;
    float* rData = r;
    float newResidualSum[coreCount] = {};
    chunk_parallel(p.size(), [alpha, pData, xData, AtApData, rData, &newResidualSum](uint id, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            xData[index] += alpha * pData[index];
            rData[index] -= alpha * AtApData[index];
            newResidualSum[id] += sq(rData[index]);
        }
    }, threadCount);
    float newResidual = sum(newResidualSum);
    float beta = newResidual / residualEnergy;
    updateTime.stop();

    // Computes next search direction: p[k+1] = r[k+1] + β p[k]
    nextTime.start();
    chunk_parallel(p.size(), [&](uint, uint offset, uint size) {
        for(uint index: range(offset,offset+size)) {
            pData[index] = rData[index] + beta * pData[index];
        }
    }, threadCount);
    nextTime.stop();

    totalTime.stop();
    k++;
    //mergeTime/totalTime, updateTime/totalTime, nextTime/totalTime); //,'\t',residualEnergy,'\\',newResidual,'=',beta, time);
    uint64 gatherTime=0, scatterTime=0, lookupTime=0, dataTime=0;
    for(const Profile& p: profile) { gatherTime+=p.gatherTime; scatterTime+=p.scatterTime; lookupTime+=p.lookupTime; dataTime+=p.dataTime; }
    log(k, str(totalTime.toFloat()/k)+"s"_, "gather:", percent(gatherTime,AtApTime), "scatter:", percent(scatterTime,AtApTime), "(", percent(lookupTime,scatterTime), percent(dataTime,scatterTime),")");
    residualEnergy = newResidual;
    return true;
}
