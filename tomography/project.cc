#include "project.h"
#include "simd.h"
#include "thread.h"

// SIMD constants for cylinder intersections
static const v4sf _2f = float4( 2 );
#define FLT_MAX __FLT_MAX__
static const v4sf mfloatMax = float4(-FLT_MAX);
static const v4sf floatMax = float4(FLT_MAX);
static const v4sf signPPNN = (v4sf)(v4si){0,0,(int)0x80000000,(int)0x80000000};
static const v4sf floatMMMm = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX};

/// Accumulates (i.e projects) (forward=true) or updates (i.e backprojects) (forward=false)
/// \note update: add=true adds, add=false multiplies
template<bool forward, bool add=true, bool trilinear=false> void projectT(const VolumeF& volume, const ImageF& image, mat4 view) {
    const float stepSize = 1; // FIXME

    // Volume
    int3 size = volume.sampleCount;
    assert_(volume.tiled() && size.x == size.y);
    const float radius = size.x/2-1-1, halfHeight = size.z/2-1-1; // Cylinder parameters (FIXME: margins)
    const v4sf capZ = {halfHeight, halfHeight, -halfHeight, -halfHeight};
    const v4sf radiusSqHeight = {radius*radius, radius*radius, halfHeight, halfHeight};
    const v4sf radiusR0R0 = {radius*radius, 0, radius*radius, 0};
    float* const volumeData = volume;
    const v4sf center = {(float)volume.sampleCount.x/2, (float)volume.sampleCount.y/2, (float)volume.sampleCount.z/2, 0};
    const uint64* const offsetX = volume.offsetX.data;// + volume.sampleCount.x/2; // + sampleCount/2 to avoid converting from centered cylinder to unsigned in inner loop
    const uint64* const offsetY = volume.offsetY.data;// + volume.sampleCount.y/2;
    const uint64* const offsetZ = volume.offsetZ.data;// + volume.sampleCount.z/2;

    // Image
    #define tileSize 8
    int imageX = floor(tileSize,image.width), imageY = floor(tileSize,image.height), imageStride = image.width; // Target image size
    assert_(imageX%tileSize ==0 && imageY%tileSize ==0);
    float* const imageData = (float*)image.data;

    // View
    mat4 world = view.inverse().scale(max(max(size.x,size.y),size.z)); // Transform normalized view space to world space
    vec3 vViewStepX = world * vec3(1./min(imageX,imageY),0,0); v4sf viewStepX = vViewStepX;
    vec3 vViewStepY = world * vec3(0,1./min(imageX,imageY),0); v4sf viewStepY = vViewStepY;
    const v4sf worldOrigin = world * vec3(0,0,0) - float(imageX/2)*vViewStepX - float(imageY/2)*vViewStepY;
    vec3 worldRay = stepSize * normalize( view.transpose() * vec3(0,0,1) );
    const v4sf ray = {worldRay.x, worldRay.y, worldRay.z, stepSize};
    const v4sf rayZ = float4(worldRay.z);
    const v4sf raySlopeZ = float4(1/worldRay.z);
    const v4sf rayXYXY = {worldRay.x, worldRay.y, worldRay.x, worldRay.y};
    const float a = worldRay.x*worldRay.x+worldRay.y*worldRay.y;
    const v4sf _m4a_4_m4a_4 = {-4*a, 4, -4*a, 4};
    const v4sf rcp_2a = float4(-1./(2*a));

    parallel(imageX/tileSize*imageY/tileSize, [&](uint, uint i) {
        const int tileX = i%(imageX/tileSize), tileY = i/(imageX/tileSize);
        float* const image = imageData+tileY*tileSize*imageStride+tileX*tileSize;
        const v4sf tileOrigin = worldOrigin + float4(tileX * tileSize) * viewStepX + float4(tileY * tileSize) * viewStepY;
        for(uint y=0; y<tileSize; y++) for(uint x=0; x<tileSize; x++) {
            const v4sf origin = tileOrigin + float4(x) * viewStepX + float4(y) * viewStepY;

            // Intersects cap disks
            const v4sf originZ = shuffle(origin, origin, 2,2,2,2);
            const v4sf capT = (capZ - originZ) * raySlopeZ; // top top bottom bottom
            const v4sf originXYXY = shuffle(origin, origin, 0,1,0,1);
            const v4sf capXY = originXYXY + capT * rayXYXY;
            const v4sf capR = dot2(capXY, capXY); // top bottom top bottom
            // Intersect cylinder side
            const v4sf originXYrayXY = shuffle(origin, ray, 0,1,0,1); // Ox Oy Dx Dy
            const v4sf cbcb = dot2(originXYXY, originXYrayXY); // OO OD OO OD (b=2OD)
            const v4sf _1b1b = blendps(_1f, cbcb, 0b1010); // 1 OD 1 OD
            const v4sf _4ac_bb = _m4a_4_m4a_4 * (cbcb*_1b1b - radiusR0R0); // -4ac bb -4ac bb
            const v4sf sqrtDelta = sqrt( hadd(_4ac_bb,_4ac_bb) );
            const v4sf sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
            const v4sf sideT = (_2f*_1b1b + sqrtDeltaPPNN) * rcp_2a; // ? t+ ? t-
            const v4sf sideZ = abs(originZ + sideT * rayZ); // ? z+ ? z-
            const v4sf capSideP = shuffle(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
            const v4sf tMask = radiusSqHeight > capSideP;
            if(!mask(tMask)) { if(forward) image[y*imageStride+x] = 0; continue; }

            const v4sf capSideT = shuffle(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
            const v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
            const v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
            v4sf position = center + origin + tmin * ray;
            const v4sf texit = center + max(floatMMMm, tmax); // max, max, max, tmax
            float length = tmax[0]-tmin[0]; // Ray length (in steps)
            float sum = 0; // Accumulates/Updates samples along the ray
            if(!forward) {
                if(length<1) continue;
                sum = image[y*imageStride+x] / floor(length);
            }
            for(;;) {
                // Lookups sample offsets
                if(trilinear) {
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
                    if(forward) {
                        const v4sf dpfv = dot4(sw_yz, x0000*cx0 + x1111*cx1);
                        sum += dpfv[0]; // Accumulates trilinearly interpolated sample
                    } else { //TODO: SIMD scatter ?
                        static_assert(add,"");
                        volumeData[vx0 + vy0 + vz0] = max(0.f, volumeData[vx0 + vy0 + vz0] + _1mpc[0] * _1mpc[1] * _1mpc[2]*sum);
                        volumeData[vx1 + vy0 + vz0] = max(0.f, volumeData[vx1 + vy0 + vz0] +    pc[0] * _1mpc[1] * _1mpc[2]*sum);
                        volumeData[vx0 + vy1 + vz0] = max(0.f, volumeData[vx0 + vy1 + vz0] + _1mpc[0] *    pc[1] * _1mpc[2]*sum);
                        volumeData[vx1 + vy1 + vz0] = max(0.f, volumeData[vx1 + vy1 + vz0] +    pc[0] *    pc[1] * _1mpc[2]*sum);
                        volumeData[vx0 + vy0 + vz1] = max(0.f, volumeData[vx0 + vy0 + vz1] + _1mpc[0] * _1mpc[1] *    pc[2]*sum);
                        volumeData[vx1 + vy0 + vz1] = max(0.f, volumeData[vx1 + vy0 + vz1] +    pc[0] * _1mpc[1] *    pc[2]*sum);
                        volumeData[vx0 + vy1 + vz1] = max(0.f, volumeData[vx0 + vy1 + vz1] + _1mpc[0] *    pc[1] *    pc[2]*sum);
                        volumeData[vx1 + vy1 + vz1] = max(0.f, volumeData[vx1 + vy1 + vz1] +    pc[0] *    pc[1] *    pc[2]*sum);
                    }
                } else {
                    const v4si p = cvtps2dq(position);
                    const uint vx = offsetX[p[0]], vy = offsetY[p[1]], vz = offsetZ[p[2]]; //FIXME: gather
                    if(forward) sum += volumeData[vx + vy + vz]; // Accumulates nearest sample
                    else if(add) volumeData[vx + vy + vz] = max(0.f, volumeData[vx + vy + vz] + sum); else volumeData[vx + vy + vz] *= sum; // Updates nearest sample
                }
                position = position + ray; // Step
                if(mask(position > texit)) break; // Check for exit intersection or saturation
            }
            if(forward) image[y*imageStride+x] = sum;
        }
    } );
}

void project(const ImageF& target, const VolumeF& source, mat4 projection) { return projectT<true,true,false>(source, target, projection); }
void projectTrilinear(const ImageF& target, const VolumeF& source, mat4 projection) { return projectT<true,true,true>(source, target, projection); }
void update(const VolumeF& target, const ImageF& source, mat4 projection) { return projectT<false,true,false>(target, source, projection); }
