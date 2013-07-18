#include "render.h"
#include "thread.h"
#include "simd.h"
#include "volume-operation.h"

void render(Image& target, const Volume8& density, /*const Volume8& intensity, const Volume8& empty,*/ mat3 view) {
    assert_(density.tiled() /*&& intensity.tiled() && empty.tiled()*/);
    // Volume
    int3 size = density.sampleCount-2*density.margin;
    assert_(size.x == size.y);
    const float radius = size.x/2-1, halfHeight = size.z/2-1; // Cylinder parameters
    const v4sf capZ = {halfHeight, halfHeight, -halfHeight, -halfHeight};
    const v4sf radiusSqHeight = {radius*radius, radius*radius, halfHeight, halfHeight};
    const v4sf radiusR0R0 = {radius*radius, 0, radius*radius, 0};
    const uint8* const densityData = density;
    /*const uint8* const intensityData = intensity;
    const uint8* const emptyData = empty;*/
    const uint64* const offsetX = density.offsetX.data + density.sampleCount.x/2; // + sampleCount/2 to avoid converting from centered cylinder to unsigned in inner loop
    const uint64* const offsetY = density.offsetY.data + density.sampleCount.y/2;
    const uint64* const offsetZ = density.offsetZ.data + density.sampleCount.z/2;

    // Image
    #define tileSize 8
    int imageX = floor(tileSize,target.width), imageY = floor(tileSize,target.height), imageStride=target.stride; // Target image size
    assert_(imageX%tileSize ==0 && imageY%tileSize ==0);
    byte4* const imageData = target.data;

    // View
    mat3 world = view.inverse().scale(max(max(size.x,size.y),size.z)*sqrt(2.)); // Transform normalized view space to world space
    vec3 vViewStepX = world * vec3(1./min(imageX,imageY),0,0); v4sf viewStepX = vViewStepX;
    vec3 vViewStepY = world * vec3(0,1./min(imageX,imageY),0); v4sf viewStepY = vViewStepY;
    const v4sf worldOrigin = world * vec3(0,0,0) - float(imageX/2)*vViewStepX - float(imageY/2)*vViewStepY;
    vec3 worldRay = normalize( view.transpose() * vec3(0,0,1) );
    const v4sf ray = {worldRay.x, worldRay.y, worldRay.z, 1};
    const v4sf rayZ = float4(worldRay.z);
    const v4sf raySlopeZ = float4(1/worldRay.z);
    const v4sf rayXYXY = {worldRay.x, worldRay.y, worldRay.x, worldRay.y};
    const float a = worldRay.x*worldRay.x+worldRay.y*worldRay.y;
    const v4sf _m4a_4_m4a_4 = {-4*a, 4, -4*a, 4};
    const v4sf rcp_2a = float4(-1./(2*a));

    enum { Invalid=1<<0, Denormal=1<<1, DivisionByZero=1<<2, Overflow=1<<3, Underflow=1<<4, Precision=1<<5 };
    extern void setExceptions(int except);
    setExceptions(Denormal | Underflow);

    parallel(imageX/tileSize*imageY/tileSize, [&](uint, uint i) {
        const int tileX = i%(imageX/tileSize), tileY = i/(imageX/tileSize);
        byte4* const image = imageData+tileY*tileSize*imageStride+tileX*tileSize;
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
            const v4sf _1b1b = blend(_1f, cbcb, 0b1010); // 1 OD 1 OD
            const v4sf _4ac_bb = _m4a_4_m4a_4 * (cbcb*_1b1b - radiusR0R0); // -4ac bb -4ac bb
            const v4sf sqrtDelta = sqrt( hadd(_4ac_bb,_4ac_bb) );
            const v4sf sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
            const v4sf sideT = (_2f*_1b1b + sqrtDeltaPPNN) * rcp_2a; // ? t+ ? t-
            const v4sf sideZ = abs(originZ + sideT * rayZ); // ? z+ ? z-
            const v4sf capSideP = shuffle(capR, sideZ, 0, 1, 1, 3); //world positions for top bottom +side -side
            const v4sf capSideT = shuffle(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
            const v4sf tMask = radiusSqHeight > capSideP;
            if(!mask(tMask)) { image[y*imageStride+x] = byte4(0,0,0,0xFF); continue; }
            const v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
            const v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
            const v4sf texit = max(floatMMMm, tmax); // max, max, max, tmax
            v4sf position = origin + tmin * ray;
            v4sf accumulator = _0f;
            for(;;) {
                // Lookup sample offsets
                const v4si p0 = cvtps2dq(position);
                const uint vx0 = offsetX[extracti(p0,0)];
                const uint vy0 = offsetY[extracti(p0,1)];
                const uint vz0 = offsetZ[extracti(p0,2)];
                /*const uint r = emptyData[vx0 + vy0 + vz0];
                if(r) position = position + cvtdq2ps(set1(r)) * ray; // Step
                else*/ {
                    const v4si p1 = p0 +_1i;
                    const uint vx1 = offsetX[extracti(p1,0)];
                    const uint vy1 = offsetY[extracti(p1,1)];
                    const uint vz1 = offsetZ[extracti(p1,2)];
                    // Loads samples (FIXME: interleave density and intensity)
                    const v4si icx0_density = {densityData[vx0 + vy0 + vz0], densityData[vx0 + vy0 + vz1], densityData[vx0 + vy1 + vz0], densityData[vx0 + vy1 + vz1]};
                    const v4si icx1_density = {densityData[vx1 + vy0 + vz0], densityData[vx1 + vy0 + vz1], densityData[vx1 + vy1 + vz0], densityData[vx1 + vy1 + vz1]};
                    /*const v4si icx0_intensity = {intensityData[vx0 + vy0 + vz0], intensityData[vx0 + vy0 + vz1], intensityData[vx0 + vy1 + vz0], intensityData[vx0 + vy1 + vz1]};
                    const v4si icx1_intensity = {intensityData[vx1 + vy0 + vz0], intensityData[vx1 + vy0 + vz1], intensityData[vx1 + vy1 + vz0], intensityData[vx1 + vy1 + vz1]};*/
                    // Compute trilinear interpolation coefficients
                    const v4sf pc = position - cvtdq2ps(p0);
                    const v4sf _1mpc = _1f - pc;
                    const v4sf z0011 = shuffle(_1mpc, pc, 2,2,2,2);
                    const v4sf z0101 = shuffle(z0011, z0011, 0,2,0,2);
                    const v4sf y0011 = shuffle(_1mpc, pc, 1,1,1,1);
                    const v4sf y0101 = shuffle(y0011, y0011, 0,2,0,2);
                    const v4sf x0000 = shuffle(_1mpc, _1mpc, 0,0,0,0);
                    const v4sf x0011 = shuffle(_1mpc, pc, 0,0,0,0);
                    const v4sf x1111 = shuffle(pc, pc, 0,0,0,0);
                    const v4sf sw_yz = z0101 * y0011;
                    // Discrete gradient from density
                    const v4sf cx0 = cvtdq2ps(icx0_density);
                    const v4sf cx1 = cvtdq2ps(icx1_density);
                    const v4sf dx = y0011*z0101*(cx0-cx1);
                    const v4sf dy = x0011*z0101*(shuffle(cx0,cx1, 0,1,0,1)-shuffle(cx0,cx1, 2,3,2,3));
                    const v4sf dz = x0011*y0101*(shuffle(cx0,cx1, 0,2,0,2)-shuffle(cx0,cx1, 1,3,1,3));
                    // Trilinearly interpolated intensity
                    const v4sf intensity = scaleFrom8bit * dot4(sw_yz, x0000*cvtdq2ps(icx0_density) + x1111*cvtdq2ps(icx1_density));
                    //const v4sf intensity = scaleFrom8bit * dot4(sw_yz, x0000*cvtdq2ps(icx0_intensity) + x1111*cvtdq2ps(icx1_intensity));
                    // Surface normal
                    const v4sf dp = transpose(dx, dy, dz, _0001f);
                    const v4sf n = dp * rsqrt(dot3(dp,dp));
                    const v4sf alpha = min(intensity, _0001f);
                    accumulator = accumulator + alpha * (_1f - shuffle(accumulator, accumulator, 3,3,3,3)) + max(intensity*intensity*_halff*(n+_1110f), alpha); // Blend
                    position = position + ray; // Step
                }
                if(mask(bitOr(accumulator > alphaTerm, position > texit))) break; // Check for exit intersection or saturation
            }
            v4si bgra32 = cvtps2dq(scaleTo8bit * accumulator);
            v8hi bgra16 = packus(bgra32, bgra32);
            v16qi bgra8 = packus(bgra16, bgra16);
            uint bgra = extracti((v4si)bgra8, 0);
            const byte4& linear = (byte4&)bgra;
            extern uint8 sRGB_lookup[256];
            image[y*imageStride+x] = byte4(sRGB_lookup[linear.b], sRGB_lookup[linear.g], sRGB_lookup[linear.r], 0xFF);
        }
    } );
}
