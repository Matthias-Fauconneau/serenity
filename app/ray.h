#pragma once
#include "math.h"
#include "simd.h"
#include "mwc.h"
#include "vector.h"

// Uniformly distributed points on sphere (Marsaglia)
static inline Vec<v8sf, 3> sphere(Random& random) {
    v8sf t0, t1, sq;
    do {
        t0 = random()*2-1;
        t1 = random()*2-1;
        sq = t0*t0 + t1*t1;
    } while(mask(sq >= 1)); // FIXME: TODO: Partial accepts
    const v8sf r = sqrt(1-sq);
    return {{2*t0*r, 2*t1*r, 1-2*sq}};
}

// Uniformly distributed points on hemisphere directed towards N
static inline Vec<v8sf, 3> hemisphere8(Random& random, vec3 N) {
    const Vec<v8sf, 3> S = sphere(random);
    const v8si negate = (N.x*S._[0] + N.y*S._[1] + N.z*S._[2]) < 0;
    return {{blend(S._[0], -S._[0], negate),
             blend(S._[1], -S._[1], negate),
             blend(S._[2], -S._[2], negate)}};
}
static inline vec3 hemisphere(Random& random, vec3 N) {
    const Vec<v8sf, 3> H8 = ::hemisphere8(random, N);
    return vec3(H8._[0][0], H8._[1][0], H8._[2][0]);
}

static inline Vec<v8sf, 2> cossin(const v8sf angle) {
  const v8sf absAngle = abs(angle);
  const v8si octant = (cvtt((4/π) * absAngle)+1) & (~1);
  const v8si cosSign = ((~(octant-2))&4) << 29;
  const v8si swap = (octant&4) << 29;
  const v8si sinSign = swap ^ sign(angle);
  const v8sf y = toFloat(octant);
  const v8sf x = float32x8(-3.77489497744594108e-8) * y + (float32x8(-2.4187564849853515625e-4) * y + (float32x8(-0.78515625) * y + absAngle));
  const v8sf x2 = x*x;
  const v8sf y1 = (((float32x8(2.443315711809948e-5) * x2 + float32x8(-1.388731625493765e-3)) * x2 + float32x8(4.166664568298827e-2)) * x2 + float32x8(-1./2)) * x2 + 1; // 0 <= x <= π/4
  const v8sf y2 = (((float32x8(-1.9515295891E-4) * x2 + float32x8(8.3321608736E-3)) * x2 + float32x8(-1.6666654611E-1)) * x2 + 1) * x; // -π/4 <= x <= 0
  const v8si select = (octant&2) == 0;
  return {{xor(cosSign, blend(y1, y2, ~select)), xor(sinSign, blend(y1, y2, select))}};
}

static inline Vec<v8sf, 3> cosine(const v8sf ξ1, const v8sf ξ2) {
    const v8sf cosθ = sqrt(1-ξ1);
    const v8sf sinθ = sqrt(ξ1);
    const v8sf φ = 2*π*ξ2;
    const Vec<v8sf, 2> cossinφ = cossin(φ);
    return {{sinθ * cossinφ._[0], sinθ * cossinφ._[1], cosθ}};
}
static inline Vec<v8sf, 3> cosine(Random& random) { return cosine(random(),random()); }

vec3 transform(const vec3 T, const vec3 B, const vec3 N, const vec3 Dl) {
    const float Dx = T.x * Dl.x + B.x * Dl.y + N.x * Dl.z;
    const float Dy = T.y * Dl.x + B.y * Dl.y + N.y * Dl.z;
    const float Dz = T.z * Dl.x + B.z * Dl.y + N.z * Dl.z;
    return vec3(Dx, Dy, Dz);
}

vec3 cosineDistribution(Random& random, const vec3 T, const vec3 B, const vec3 N) {
    const Vec<v8sf, 3> D8 = ::cosine(random); // Random ray direction drawn from cosine probability distribution
    return transform(T,B,N, vec3(D8._[0][0], D8._[1][0], D8._[2][0]));
}

inline void cross(const v8sf Ax, const v8sf Ay, const v8sf Az, const v8sf Bx, const v8sf By, const v8sf Bz, v8sf& X, v8sf& Y, v8sf& Z) {
    X = Ay*Bz - By*Az;
    Y = Az*Bx - Bz*Ax;
    Z = Ax*By - Bx*Ay;
}

inline v8sf dot(const v8sf Ax, const v8sf Ay, const v8sf Az, const v8sf Bx, const v8sf By, const v8sf Bz) {
    return Ax*Bx + Ay*By + Az*Bz;
}

// "Fast, Minimum Storage Ray/Triangle Intersection"
inline v8sf intersect(const v8sf xA, const v8sf yA, const v8sf zA,
                      const v8sf xB, const v8sf yB, const v8sf zB,
                      const v8sf xC, const v8sf yC, const v8sf zC,
                      const v8sf  Ox, const v8sf  Oy, const v8sf Oz,
                      const v8sf  Dx, const v8sf  Dy, const v8sf Dz,
                      v8sf& det, v8sf& u, v8sf& v) {
    const v8sf eACx = xC - xA;
    const v8sf eACy = yC - yA;
    const v8sf eACz = zC - zA;
    v8sf Px, Py, Pz; cross(Dx, Dy, Dz, eACx, eACy, eACz, Px, Py, Pz);
    const v8sf Tx = Ox - xA;
    const v8sf Ty = Oy - yA;
    const v8sf Tz = Oz - zA;
    u = dot(Tx, Ty, Tz, Px, Py, Pz);
    const v8sf eABx = xB - xA;
    const v8sf eABy = yB - yA;
    const v8sf eABz = zB - zA;
    det = dot(eABx, eABy, eABz, Px, Py, Pz);
    v8sf Qx, Qy, Qz; cross(Tx, Ty, Tz, eABx, eABy, eABz, Qx, Qy, Qz);
    v = dot(Dx, Dy, Dz, Qx, Qy, Qz);
    const v8sf t = dot(eACx, eACy, eACz, Qx, Qy, Qz) / det;
    return blend(float32x8(inff), t, det > 0 && u >= 0 && v >= 0 && u + v <= det && t > 0);
}

// "Fast, Minimum Storage Ray/Triangle Intersection"
inline v8sf intersectTwoSided(const v8sf xA, const v8sf yA, const v8sf zA,
                      const v8sf xB, const v8sf yB, const v8sf zB,
                      const v8sf xC, const v8sf yC, const v8sf zC,
                      const v8sf  Ox, const v8sf  Oy, const v8sf Oz,
                      const v8sf  Dx, const v8sf  Dy, const v8sf Dz,
                      v8sf& det, v8sf& u, v8sf& v) {
    const v8sf eACx = xC - xA;
    const v8sf eACy = yC - yA;
    const v8sf eACz = zC - zA;
    v8sf Px, Py, Pz; cross(Dx, Dy, Dz, eACx, eACy, eACz, Px, Py, Pz);
    const v8sf eABx = xB - xA;
    const v8sf eABy = yB - yA;
    const v8sf eABz = zB - zA;
    det = dot(eABx, eABy, eABz, Px, Py, Pz);
    const v8sf Tx = Ox - xA;
    const v8sf Ty = Oy - yA;
    const v8sf Tz = Oz - zA;
    u = dot(Tx, Ty, Tz, Px, Py, Pz) / det;
    v8sf Qx, Qy, Qz; cross(Tx, Ty, Tz, eABx, eABy, eABz, Qx, Qy, Qz);
    v = dot(Dx, Dy, Dz, Qx, Qy, Qz) / det;
    const v8sf t = dot(eACx, eACy, eACz, Qx, Qy, Qz) / det;
    return blend(float32x8(inff), t, det != 0 && u >= 0 && v >= 0 && (u + v) <= 1 && t > 0);
}

inline v8sf hmin(const v8sf x) {
    const v8sf v0 = __builtin_ia32_minps256(x, __builtin_ia32_palignr256(x, x, 4));
    const v8sf v1 = __builtin_ia32_minps256(v0, __builtin_ia32_palignr256(v0, v0, 8));
    return __builtin_ia32_minps256(v1, __builtin_ia32_permti256(v1, v1, 0x01));
}

inline uint indexOfEqual(const v8sf x, const v8sf y) {
    return __builtin_ctz(::mask(x == y));
}
