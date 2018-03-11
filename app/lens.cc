#include "thread.h"
#include "vector.h"
#include "math.h"

struct Test {
    Test() {
        const float μ = 2.40; //μm
        const uint2 pixelCount = uint2(5496, 3672);
        const float d_y = pixelCount.y * μ;
#if 1
        const float lp_mm = 150;
        const float C = 1000/lp_mm;
#else
        const float C = 2*μ;
#endif
        const float N = 16; // f#N

#if 0
        #define Dn Dn
        const float Dn = 3 * 25400; //μm
#endif

#if 0
        #define f f
        const float f = 8000;
#endif

#if 1
        #define e e
        const float e = 250;
#endif

#if 1
        #define α α
        const float α_y = 55*π/180;
        #define v v
        const float v = d_y / (2*tan(α_y/2));
#endif

#ifndef f //(Dn, v)
        #define f f
#ifdef e
        const float f = v-e;
#else
        const float a = 1/Dn+1/v;
        const float b = -1;
        const float c = -N*C;
        const float Δ = b*b - 4*a*c;
        const float f = (-b+sqrt(Δ))/(2*a);
        assert_(Δ>0 && -b-sqrt(Δ) < 0 && f>0);
#endif
#endif
        const float H = (f*f)/(N*C);
#ifndef s
#ifdef v
        const float s = 1/(1/f - 1/v);
#elif Dn// s (Dn)
        const float s = (Dn*H)/(H-Dn);
#else
        const float s = inff;
        const float v = 1/(1/f-1/s);
#endif
#endif
#ifndef e
        const float e = v-f;
#endif
#ifndef Dn
        const float Dn = s==inff ? H : H*s/(H+s);
#endif
        const float Df = H*s/(H-s);

#ifndef α
        const float α_y = 2*atan(d_y, 2*v);
#endif

        assert_(v==f+e);

        log("α~"+str(α_y*180/π)+"°");
        log("s~"+str(s/10000)+"cm");
        log("f~"+str(f/1000)+"mm", "e~"+str(e/1000)+"mm");
        log("v~"+str(v/1000)+"mm");
        log("Dn~"+str(Dn/10000)+"cm", "Df~"+str(Df/10000)+"cm", "δ~"+str((Df-Dn)/10000)+"cm");
    }
} static test;
