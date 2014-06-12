#include "SART.h"
#include "sum.h"
#include "time.h"

SART::SART(int3 size, const ImageArray& b) : Reconstruction(size, b), Ai(b.size), Ati(size), Ax(b.size), h(b.size), L(size) {
    {buffer<float> support (size.x*size.y*size.z);
        vec2 center = vec2(size.xy()-int2(1))/2.f; float radiusSq = sq(center);
        for(uint z: range(size.z)) for(uint y: range(size.y)) for(uint x: range(size.x)) if(sq(vec2(x,y)-center) <= radiusSq) support[z*size.y*size.x + y*size.x + x] = 1;
        CLVolume i (size, support);
        project(Ai, i); // Projects identity volume
    }
    {buffer<float> support (b.size.x*b.size.y*b.size.z); support.clear(1);
        ImageArray i (b.size, support);
        backproject(Ati, At, i); // Backprojects identity projections
    }
}

CL(operators, delta) void delta(const CLVolume& y, const CLVolume& a, const CLVolume& b, const CLVolume& c) { CL::delta(y.size, y, a, b, c); } // y = ( a - b ) / c
CL(operators, update) void update(const CLVolume& y, const CLVolume& a, const float lambda, const CLVolume& b, const CLVolume& c) { CL::update(y.size, y, a, lambda, b, c); } // y = a + c ? λ b / c : 0 > 0 ? : 0

void SART::step() {
    Time time;
    project(Ax, x); // Ax = A x
    delta(h, b, Ax, Ai); // h = ( b - Ax ) / Ai
    backproject(L, At, h); // L = At h
    const float lambda = 1./4;
    update(x, x, lambda, L, Ati); // x = | x + λ L / Ati |+

    k++; totalTime += time;
}

