#include "project.h"
#include "thread.h"

// SIMD constants for cylinder intersections
static const v4sf _2f = float4( 2 );
#define FLT_MAX __FLT_MAX__
static const v4sf mfloatMax = float4(-FLT_MAX);
static const v4sf floatMax = float4(FLT_MAX);
static const v4sf signPPNN = (v4sf)(v4si){0,0,(int)0x80000000,(int)0x80000000};
static const v4sf floatMMMm = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX};

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
    const v4sf sqrtDelta = sqrt( hadd(_4ac_bb,_4ac_bb) );
    const v4sf sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
    const v4sf sideT = (_2f*_1b1b + sqrtDeltaPPNN) * p.rcp_2a; // ? t+ ? t-
    const v4sf sideZ = abs(originZ + sideT * p.rayZ); // ? z+ ? z-
    const v4sf capSideP = shuffle(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
    const v4sf tMask = radiusSqHeight > capSideP;
    if(!mask(tMask)) { length=0; return 0; }

    const v4sf capSideT = shuffle(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
    const v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
    const v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
    length = tmax[0]-tmin[0]; // Ray length (in steps)
    v4sf position = volumeOrigin + origin + tmin * p.ray;
    const v4sf texit = max(floatMMMm, tmax); // max, max, max, tmax
    float sum = 0; // Accumulates/Updates samples along the ray
    do {
        //#define str4(v) str(#v, v[0], v[1], v[2], v[3])
        //log(str4(origin), str4(position), tmax[3]);
        // Lookups sample offsets
        const v4si p0 = cvttps2dq(position);
        const uint vx0 = offsetX[p0[0]], vy0 = offsetY[p0[1]], vz0 = offsetZ[p0[2]]; //FIXME: gather
        const v4si p1 = p0 +_1i;
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
        sum += dpfv[0]; // Accumulates trilinearly interpolated sample
        position = position + p.ray; // Step
    } while(!mask(position > texit)); // Check for exit intersection
    return sum; //2 * sum / volume.sampleCount.x;
}

void project(const ImageF& image, CylinderVolume volume, Projection projection) {
    // Image
    constexpr uint tileSize = 8;
    int imageX = floor(tileSize,image.width), imageY = floor(tileSize,image.height), imageStride = image.width; // Target image size
    assert_(imageX%tileSize ==0 && imageY%tileSize ==0);
    float* const imageData = (float*)image.data;
    vec3 vViewStepX = projection.world * vec3(1,0,0); v4sf viewStepX = vViewStepX;
    vec3 vViewStepY = projection.world * vec3(0,1,0); v4sf viewStepY = vViewStepY;
    const v4sf worldOrigin = projection.world * vec3(0,0,0) - float(imageX/2)*vViewStepX - float(imageY/2)*vViewStepY;

    parallel(imageX/tileSize*imageY/tileSize, [&](uint, uint i) {
        const int tileX = i%(imageX/tileSize), tileY = i/(imageX/tileSize);
        float* const image = imageData+tileY*tileSize*imageStride+tileX*tileSize;
        const v4sf tileOrigin = worldOrigin + float4(tileX * tileSize) * viewStepX + float4(tileY * tileSize) * viewStepY;
        for(uint y=0; y<tileSize; y++) for(uint x=0; x<tileSize; x++) {
            const v4sf origin = tileOrigin + float4(x) * viewStepX + float4(y) * viewStepY;
            float length;
            image[y*imageStride+x] = volume.accumulate(projection, origin, length);
        }
    } );
}

void updateSIRT(CylinderVolume volume, const map<Projection, ImageF>& projections) {
    chunk_parallel(volume.volume.size(), [&](uint, uint offset, uint size) {
        const vec3 center = vec3(volume.size-int3(1))/2.f;
        const vec2 imageCenter = vec2(projections.values[0].size())/2.f;
        const float radiusSq = sq(center.x);
        for(uint index: range(offset, offset+size)) {
            const vec3 origin = vec3(zOrder(index)) - center;
            if(sq(origin.xy()) >= radiusSq) continue;
            float projectionSum = 0, lengthSum = 0;
            float reconstructionSum = 0, pointCount = 0;
            for(uint projectionIndex: range(projections.size())) {
                const Projection& projection = projections.keys[projectionIndex];
                // Accumulate
                float length;
                float sum = volume.accumulate(projection, origin, length);
                reconstructionSum += sum;
                lengthSum += length;
                pointCount += floor(length);
                // Sample
                const ImageF& P = projections.values[projectionIndex];
                vec2 xy = (projection.projection * origin).xy() + imageCenter;
                uint i = xy.x, j = xy.y;
                float u = fract(xy.x), v = fract(xy.y);
                float s = (1-v) * ((1-u) * P(i,j  ) + u * P(i+1,j  )) +
                           v    * ((1-u) * P(i,j+1) + u * P(i+1,j+1));
                projectionSum += s;
            }
            // Update
            float& v = volume.volumeData[index];
            v = max(0.f, v + projectionSum/lengthSum - reconstructionSum/pointCount);
        }
    });
}

//void updateSART(const VolumeF& target, const ImageF& source, mat4 projection) {}
