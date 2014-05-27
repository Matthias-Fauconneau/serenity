struct Projection { float4 origin, ray[3]; };
struct Volume { float4 capZ, radiusR0R0, radiusSqHeight, dataOrigin; };
__kernel void project(const uint width, const struct Projection p, const struct Volume v, __global float* image, __global const float* volume) {
    size_t x = get_global_id(0);
    size_t y = get_global_id(1);
    size_t i = y * width + x;

    float4 ray =  blendps(_1f, normalize3(x * p.ray[0] + y * p.ray[1] + p.ray[2]), 0b0111);
    // Intersects cap disks
    const v4sf originZ = shuffle4(p.origin, p.origin, 2,2,2,2);
    const v4sf capT = (v.capZ - originZ) * rcp(shuffle4(ray, ray, 2,2,2,2)); // top top bottom bottom
    const v4sf originXYXY = shuffle4(p.origin, p.origin, 0,1,0,1);
    const v4sf rayXYXY = shuffle4(ray, ray, 0,1,0,1);
    const v4sf capXY = originXYXY + capT * rayXYXY;
    const v4sf capR = dot2(capXY, capXY); // top bottom top bottom
    // Intersect cylinder side
    const v4sf originXYrayXY = shuffle4(p.origin, ray, 0,1,0,1); // Ox Oy Dx Dy
    const v4sf cbcb = dot2(originXYXY, originXYrayXY); // OO OD OO OD (b=2OD)
    const v4sf _1b1b = blendps(_1f, cbcb, 0b1010); // 1 OD 1 OD
    const v4sf a = dot2(rayXYXY, rayXYXY); // x²+y²
    const v4sf _4ac_bb = (m4_0_m4_0 * a + _0404f) * (cbcb*_1b1b - v.radiusR0R0); // -4ac 4bb -4ac 4bb
    const v4sf delta = hadd(_4ac_bb,_4ac_bb);
    const v4sf sqrtDelta = sqrt( delta );
    const v4sf sqrtDeltaPPNN = bitOr(sqrtDelta, signPPNN); // +delta +delta -delta -delta
    const v4sf sideT = (_2f*_1b1b + sqrtDeltaPPNN) *  rcp(_m2f*a); // ? t+ ? t-
    const v4sf sideZ = abs(originZ + sideT * float4(ray[2])); // ? z+ ? z-
    const v4sf capSideP = shuffle4(capR, sideZ, 0, 1, 1, 3); // topR2 bottomR2 +sideZ -sideZ
    const v4sf tMask = capSideP < v.radiusSqHeight;
    if(!mask(tMask)) { image[i] = 0; return; }
    const v4sf capSideT = shuffle4(capT, sideT, 0, 2, 1, 3); //ray position (t) for top bottom +side -side
    v4sf tmin = hmin( blendv(floatMax, capSideT, tMask) );
    v4sf tmax = hmax( blendv(mfloatMax, capSideT, tMask) );
    start = v.dataOrigin + p.origin + tmin * ray;
    end = max(floatMMMm, tmax); // max, max, max, tmax
    image[i] = 1;
}
