#include "thread.h"
#include "vector.h"
#include "math.h"
struct Test {
    Test() {
        const vec3 A (0.22, 0.25, 0.94);
        const vec3 B (0.20, 0.34, 0.92);
        log(acos(dot(A,B)/sqrt(dotSq(A)*dotSq(B)))*180/π);
    }
} static test;
