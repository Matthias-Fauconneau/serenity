#include "thread.h"
#include "vector.h"
#include "math.h"

struct Test {
    Test() {
        const float μ = 2.40; //μm
#if 1
        const float lp_mm = 150;
        const float C = 1000/lp_mm;
#else
        const float C = 2*μ;
#endif
        const uint2 pixelCount = uint2(5496, 3672);
        const float d_y = pixelCount.y * μ;
        const float α_y = 55*π/180;
        const float N = 16; // f#N
#if 0
        #define Dn Dn
        const float Dn = 3 * 25400; //μm
#endif
        const float v = d_y / (2*tan(α_y/2));
#ifndef f
        #define f f
#ifndef Dn
        const float f = 6500;
#else //(Dn, v)
        const float a = 1/Dn+1/v;
        const float b = -1;
        const float c = -N*C;
        const float Δ = b*b - 4*a*c;
        const float f = (-b+sqrt(Δ))/(2*a);
        assert_(Δ>0 && -b-sqrt(Δ) < 0 && f>0);
#endif
#endif
        const float H = (f*f)/(N*C);
#ifdef s
#ifdef Dn// s (Dn)
        const float s = (Dn*H)/(H-Dn);
#else
        const float s = inff;
        const float v = 1/(1/f-1/s);
#endif
#else
        const float s = 1/(1/f - 1/v);
#endif
#ifndef Dn
        const float Dn = s==inff ? H : H*s/(H+s);
#endif
        const float Df = H*s/(H-s);
        const float e = v-f;
        log("s~"+str(s/10000)+"cm");
        log("f~"+str(f/1000)+"mm", "e~"+str(e/1000)+"mm");
        log("v~"+str(v/1000)+"mm");
        log("Dn~"+str(Dn/10000)+"cm", "Df~"+str(Df/10000)+"cm", "δ~"+str((Df-Dn)/10000)+"cm");
    }
} static test;
