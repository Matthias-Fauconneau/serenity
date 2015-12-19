#pragma once
#include "memory.h"
#include "vector.h"
#include "simd.h"

#define sconst static constexpr

static inline constexpr float pow4(float x) { return x*x*x*x; }

template<> inline String str(const vXsf& a) { return str(ref<float>((float*)&a, simd)); }
template<> inline String str(const vXsi& a) { return str(ref<int>((int*)&a, simd)); }

static inline void qapply(vXsf Qx, vXsf Qy, vXsf Qz, vXsf Qw, vXsf Vx, vXsf Vy, vXsf Vz,
                          vXsf& QVx, vXsf& QVy, vXsf& QVz) {
 const vXsf X = Qw*Vx - Vy*Qz + Qy*Vz;
 const vXsf Y = Qw*Vy - Vz*Qx + Qz*Vx;
 const vXsf Z = Qw*Vz - Vx*Qy + Qx*Vy;
 const vXsf W = Vx * Qx + Vy * Qy + Vz * Qz;
 QVx = Qw*X + W*Qx + Qy*Z - Y*Qz;
 QVy = Qw*Y + W*Qy + Qz*X - Z*Qx;
 QVz = Qw*Z + W*Qz + Qx*Y - X*Qy;
}

// Units
sconst float s = 1, m = 1, kg = 1, N = kg /m /(s*s), Pa = N / (m*m);
sconst float mm = 1e-3*m, g = 1e-3*kg, KPa = 1e3 * Pa, MPa = 1e6 * Pa, GPa = 1e9 * Pa;
