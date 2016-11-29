#pragma once
#include "simd.h"

// Coefficients for minimax approximation of sin(x*pi/4), x=[0,2].
static const v8sf s1 =  0.7853975892066955566406250000000000f;
static const v8sf s2 = -0.0807407423853874206542968750000000f;
static const v8sf s3 =  0.0024843954015523195266723632812500f;
static const v8sf s4 = -0.0000341485538228880614042282104492f;

// Coefficients for minimax approximation of cos(x*pi/4), x=[0,2].
static const v8sf c1 =  0.9999932952821962577665326692990000f;
static const v8sf c2 = -0.3083711259464511647371969120320000f;
static const v8sf c3 =  0.0157862649459062213825197189573000f;
static const v8sf c4 = -0.0002983708648233575495551227373110f;

// Coefficients for 6th degree minimax approximation of atan(x)*2/pi, x=[0,1].
static const v8sf t1 =  0.406758566246788489601959989e-5f;
static const v8sf t2 =  0.636226545274016134946890922156f;
static const v8sf t3 =  0.61572017898280213493197203466e-2f;
static const v8sf t4 = -0.247333733281268944196501420480f;
static const v8sf t5 =  0.881770664775316294736387951347e-1f;
static const v8sf t6 =  0.419038818029165735901852432784e-1f;
static const v8sf t7 = -0.251390972343483509333252996350e-1f;

// [-1, 1] -> R3
inline static Vec<v8sf, 3> sphere(const v8sf u, const v8sf v) {
    const v8sf x = abs(u), y = abs(v);
    const v8sf sd = 1-(x+y);
    const v8sf r = 1-abs(sd);
    const v8sf φ = and(r != 0, (y-x)/r) + 1;
    const v8sf φ2 = φ*φ;
    const v8sf cosφ = xor(sign(u), (((c4 * φ2 + c3) * φ2 + c2) * φ2 + c1));     // c1   + c2*φ^2 + c3*φ^4 + c4*φ^6
    const v8sf sinφ = xor(sign(v), (((s4 * φ2 + s3) * φ2 + s2) * φ2 + s1) * φ); // s1*φ + s2*φ^3 + s3*φ^5 + s4*φ^7
    const v8sf r2 = r*r;
    const v8sf sinθ = r*sqrt(2-r2);
    return {{sinθ * cosφ, sinθ * sinφ, xor(sign(sd), 1-r2)/*cosθ*/}};
}

// R3 -> [-1, 1]
inline static Vec<v8sf, 2> square(const v8sf x, const v8sf y, const v8sf z) {
    const v8sf absX = abs(x), absY = abs(y);
    const v8sf a = max(absX, absY);
    const v8sf b = and(a != 0, min(absX, absY) / a);
    const v8sf φ0 = (((((t7 * b + t6) * b + t5) * b + t4) * b + t3) * b + t2) * b + t1;
    const v8sf φ = blend(φ0, 1-φ0, absX < absY);
    const v8sf r = sqrt(max(0, 1-abs(z*rsqrt(x*x+y*y+z*z))));
    const v8sf v0 = φ * r;
    const v8sf u0 = r - v0;
    const v8sf u = blend(u0, 1-v0, z<0);
    const v8sf v = blend(v0, 1-u0, z<0);
    return {{xor(sign(x), u), xor(sign(y), v)}};
}
