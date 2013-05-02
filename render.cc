#include "render.h"
#include "simd.h"

/// Tiles a volume recursively into bricks (using 3D Z ordering)
void tile(Volume& target, const Volume& source) {
    uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    interleavedLookup(target);

    for(uint z=0; z<Z; z++) {
        const uint16* const slice = source.data+z*XY;
        for(uint y=0; y<Y; y++) {
            const uint16* const line = slice+y*X;
            for(uint x=0; x<X; x++) {
                target.data[target.offsetZ[z]+target.offsetY[y]+target.offsetX[x]] = line[x];
            }
        }
    }
}

/// Clips volume data to a cylinder and sets zero samples to 1
void clip(Volume& target) {
    uint X=target.x, Y=target.y, Z=target.z;
    uint marginX=target.marginX, marginZ=target.marginZ;
    uint radiusSq=(X/2-marginX)*(X/2-marginX);
    interleavedLookup(target);

    for(uint z=0; z<Z; z++) {
        for(uint y=0; y<Y; y++) {
            for(uint x=0; x<X; x++) {
                uint16& value = target.data[target.offsetZ[z]+target.offsetY[y]+target.offsetX[x]];
                if(z > marginZ && z < Z-marginZ && (x-X/2)*(x-X/2) + (y-Y/2)*(y-Y/2) < radiusSq) {
                    if(value==0) value=1; // Light attenuation
                } else {
                    value = 0; //Clip
                }
            }
        }
    }
}

/// Renders a volume by projecting it on the image plane
Image render(const Volume& volume, mat3 view) {
    // Volume
    Q_ASSERT(volume.x==volume.y && volume.y == volume.z);
    uint stride = volume.x; // Unclipped volume data size
    uint size = stride-2*volume.marginX; // Clipped volume size
    const float radius = size/2, halfHeight = size/2; // Cylinder parameters
    const v4sf capZ = {halfHeight, halfHeight, -halfHeight, -halfHeight};
    const v4sf radiusSqHeight = {radius*radius, radius*radius, halfHeight, halfHeight};
    const v4sf radiusR0R0 = {radius*radius, 0, radius*radius, 0};
    const uint16* const data = volume.data;
    const uint* const offsetX = volume.offsetX + stride/2; // + stride/2 to avoid converting from centered cylinder to unsigned in inner loop
    const uint* const offsetY = volume.offsetY + stride/2;
    const uint* const offsetZ = volume.offsetZ + stride/2;
    Q_ASSERT(offsetX && offsetY && offsetZ);

    // Image
    int imageX = stride, imageY = stride; // Target image size
    Q_ASSERT(imageX == imageY);
    Image target(imageX, imageY);

    // View
    mat3 world = inverse(view).scale(size*sqrt(2)); // Transform normalized view space to world space
    vec3 vViewStepX = world * vec3(1./imageX,0,0); v4sf viewStepX = vViewStepX;
    vec3 vViewStepY = world * vec3(0,1./imageY,0); v4sf viewStepY = vViewStepY;
    const v4sf worldOrigin = world * vec3(0,0,0) - (imageX/2) * vViewStepX - (imageY/2)*vViewStepY;
    vec3 worldRay = normalize( view.transpose() * vec3(0,0,1) );
    const v4sf ray = {worldRay.x, worldRay.y, worldRay.z, 1};
    const v4sf rayZ = float4(worldRay.z);
    const v4sf raySlopeZ = float4(1/worldRay.z);
    const v4sf rayXYXY = {worldRay.x, worldRay.y, worldRay.x, worldRay.y};
    const float a = worldRay.x*worldRay.x+worldRay.y*worldRay.y;
    const v4sf _m4a_4_m4a_4 = {-4*a, 4, -4*a, 4};
    const v4sf rcp_2a = float4(-1./(2*a));

    #define tileSize 8
    for(int i=0; i<imageX/tileSize*imageY/tileSize; i++) {
        const int tileX = i%(imageX/tileSize), tileY = i/(imageY/tileSize);
        uint* const image = (uint*)target.data+tileY*tileSize*imageX+tileX*tileSize;
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
            const v4sf tMask = radiusSqHeight > capSideP; //cmpgt(radiusSqHeight, capSideP);
            if(!mask(tMask)) { image[y*imageX+x] = 0; continue; }
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
                const v4si p1 = p0 +_1i;
                const uint vx1 = offsetX[extracti(p1,0)];
                const uint vy1 = offsetY[extracti(p1,1)];
                const uint vz1 = offsetZ[extracti(p1,2)];
                // Loads samples
                const v4si icx0 = {data[vx0 + vy0 + vz0], data[vx0 + vy0 + vz1], data[vx0 + vy1 + vz0], data[vx0 + vy1 + vz1]};
                const v4si icx1 = {data[vx1 + vy0 + vz0], data[vx1 + vy0 + vz1], data[vx1 + vy1 + vz0], data[vx1 + vy1 + vz1]};
                // Trilinear interpolation
                const v4sf cx0 = cvtdq2ps(icx0);
                const v4sf cx1 = cvtdq2ps(icx1);
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
                const v4sf sample = scaleFrom8bit * dot4(sw_yz, x0000*cx0 + x1111*cx1);
                // Discrete gradient
                const v4sf dx = y0011*z0101*(cx0-cx1);
                const v4sf dy = x0011*z0101*(shuffle(cx0,cx1, 0,1,0,1)-shuffle(cx0,cx1, 2,3,2,3));
                const v4sf dz = x0011*y0101*(shuffle(cx0,cx1, 0,2,0,2)-shuffle(cx0,cx1, 1,3,1,3));
                // Surface normal
                const v4sf dp = transpose(dx, dy, dz, _0001f);
                const v4sf n = dp * rsqrt(dot3(dp,dp));
                const v4sf alpha = min(sample, _0001f);
                accumulator = accumulator + alpha * (_1f - shuffle(accumulator, accumulator, 3,3,3,3)) + max(sample* _halff*(n+_1110f), alpha); // Blend
                position = position + ray; // Step
                if(mask(bitOr(cmpgt(accumulator, alphaTerm), cmpgt(position, texit)))) break; // Check for exit intersection or saturation
            }
            v4si bgra32 = cvtps2dq(scaleTo8bit * accumulator);
            v8hi bgra16 = packus(bgra32, bgra32);
            v16qi bgra8 = packus(bgra16, bgra16);
            image[y*imageX+x] = extracti((v4si)bgra8, 0);
        }
    }
    return target;
}
