#pragma once
#include "math.h"

/// Returns a sequence of uniformly distributed pseudo-random 64bit integers or 32bit floats
struct Random {
    uint sz=1,sw=1;
    uint z,w;
    Random() { /*seed();*/ reset(); }
#if USE_TSC
    void seed() { sz=rdtsc(); sw=rdtsc(); }
#endif
    void reset() { z=sz; w=sw; }
    uint next() {
        z = 36969 * (z & 0xFFFF) + (z >> 16);
        w = 18000 * (w & 0xFFFF) + (w >> 16);
        return (z << 16) + w;
    }
    operator uint() { return next(); }
    float operator()() { float f = float(next()&((1<<24)-1))*0x1p-24f; assert(f>=0 && f<1); return f; }
};

inline buffer<uint> shuffleSequence(uint size) {
    buffer<uint> seq(size);
    Random random;
    for(uint i: range(size)) seq[i] = i;
    for(uint i=size-1; i>0; i--) swap(seq[i], seq[random%(i+1)]);
    return seq;
}

extern "C" float lgammaf(float x);
/// Returns a sequence of poisson distributed pseudo-random integers
/// \note The expected value \a lambda must be greater than 3.36/0.767~4.38
inline uint poisson(float lambda) {
    static Random random;
    float c = 0.767f - 3.36f/lambda;
    float beta = PI/sqrt(3*lambda);
    float alpha = beta*lambda;
    float k = ln(c) - lambda - ln(beta);
    for(;;) {
        float u = random();
        float x = (alpha - ln((1 - u)/u))/beta;
        int n = floor(x + 1./2);
        if(n < 0) continue;
        float v = random();
        float y = alpha - beta*x;
        float lhs = y + ln(v/sq(1.0 + exp(y)));
        float rhs = k + n*ln(lambda) - lgammaf(n+1);
        if (lhs <= rhs) return n;
    }
}
