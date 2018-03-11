#include "thread.h"
#include "vector.h"
#include "math.h"
struct Test {
    Test() {
        const vec3 A (-0.22, 0.25, -0.94);
        const vec3 B (-0.17, 0.39, -0.91);
        log(acos(dot(A,B)/sqrt(dotSq(A)*dotSq(B)))*180/π);
    }
} static test;
