#include "algebraic.h"
#include "sum.h"
#include "time.h"

Algebraic::Algebraic(int3 size, const ImageArray& b) : Reconstruction(size, b), AAti(size), Ax(b.size), p(size) {
    {buffer<float> ones (b.size.x*b.size.y*b.size.z); ones.clear(1);
        ImageArray i (b.size, 1.f);
        CLVolume Ati (size);
        backproject(Ati, At, i); // Backprojects identity projections
        project(AAti, Ati); // Projects coefficents volume
    }
}

CL(operators, delta) void delta(const ImageArray& y, const ImageArray& a, const ImageArray& b, const ImageArray& c) { CL::delta(y.size, y, a, b, c); } // y = ( a - b ) / c
CL(operators, update) void update(const CLVolume& y, const CLVolume& a, const CLVolume& b) { CL::update(y.size, y, a, b); } // y = max(0, a + b)

void Algebraic::step() {
    Time time;
    project(Ax, x); // Ax = A x
    const ImageArray& e = Ax; // In-place
    delta(e, b, Ax, AAti); // e = ( b - Ax ) / A At i
    backproject(p, At, e); // p = At e
    update(x, x, p); // x = max(0, x + p)

    k++; totalTime += time;
}

