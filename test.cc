#include "thread.h"
#include "simd.h"

struct Test {
    Test() {
        const float max = 2*PI;
        Time time;
        for(float angle=0; angle<=2*PI;) {
            Vec<v8sf, 2> cossin = ::cossin(angle);
            assert_(cos(angle) == cossin._[0][0]);
            assert_(sin(angle) == cossin._[1][0]);
            const int next = (int&)angle + 1;
            angle = (float&)next;
        }
        log((int&)max, time);
    }
} app;
