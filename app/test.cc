#include "thread.h"
#include "vector.h"
#include "math.h"

struct Test {
    Test() {
        const float Dn = 3 * 2540; //μm
        const float μ = 2.40; //μm
        const float C = μ;
        const uint2 pixelCount = uint2(5496, 3672);
        const float d_y = pixelCount.y * μ;
        const float α_y = 55*π/180;
        const float v = d_y / (2*tan(α_y));
        const float a = 1/Dn+1/v;
        const float b = -1;
        const float N = 1.8; // f#N
        const float c = -N*C;
        const float Δ = b*b - 4*a*c;
        const float f = (-b+sqrt(Δ))/(2*a);
        assert_(Δ>0 && -b-sqrt(Δ) < 0 && f>0);
        const float H = (f*f)/(N*C);
        const float s = 1/(1/f - 1/v);
        const float Df = H*s/(H-s);
        const float e = v-f;
        log("f~"+str(f/1000)+"mm", "v~"+str(v/1000)+"mm", "e~"+str(e/1000)+"mm");
        log("Dn~"+str(Dn/1000)+"mm", "s~"+str(s/1000)+"mm", "H~"+str(H/1000)+"mm", "Df~"+str(Df/1000)+"mm");
    }
} static test;
