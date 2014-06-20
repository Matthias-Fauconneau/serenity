#pragma once
//#include "time.h"

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

Random random;
extern "C" double lgamma(double x);
/// Returns a sequence of poisson distributed pseudo-random integers
uint poisson(double lambda) {
    double c = 0.767 - 3.36/lambda;
    double beta = PI/sqrt(3.0*lambda);
    double alpha = beta*lambda;
    double k = ln(c) - lambda - ln(beta);
    for(;;) {
        float u = random();
        double x = (alpha - ln((1.0 - u)/u))/beta;
        int n = floor(x + 1./2);
        if(n < 0) continue;
        double v = random();
        double y = alpha - beta*x;
        double lhs = y + ln(v/sq(1.0 + exp(y)));
        double rhs = k + n*ln(lambda) - lgamma(n+1);
        if (lhs <= rhs) return n;
    }
}
