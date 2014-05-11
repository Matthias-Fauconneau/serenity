#include "project.h"

struct Test {
    Test() {
        int3 volume (512,512,896);
        int2 image (504, 378);
        Projection projection(volume, image, 0);
        log(projection.project(vec3(0,0,0)));
    }
} test;
