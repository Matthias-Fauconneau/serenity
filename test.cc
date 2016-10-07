#include "simd.h"
#include "thread.h"

struct Test {
    Test() {
        v8si b[1] {{0, 1, 2, 3, 4, 5, 6, 7}};
        for(int i: range(8)) log(((int*)(b))[i], str(__builtin_ia32_movmskps256(b[0] == intX(0)), 8u, '0', 2u));
    }
} test;
