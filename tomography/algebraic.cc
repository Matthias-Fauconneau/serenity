#include "algebraic.h"
#include "operators.h"
#include "time.h"

Algebraic::Algebraic(int3 size, const ImageArray& b) : Reconstruction(size, b), AAti(size), Ax(b.size), p(size) {
    {buffer<float> ones (b.size.x*b.size.y*b.size.z); ones.clear(1);
        ImageArray i (b.size, 1.f);
        CLVolume Ati (size);
        backproject(Ati, At, i); // Backprojects identity projections
        project(AAti, Ati); // Projects coefficents volume
    }
}


void Algebraic::step() {
    time += project(Ax, x); // Ax = A x
    const ImageArray& e = Ax; // In-place
    time += delta(e, b, Ax, AAti); // e = ( b - Ax ) / A At i
    time += backproject(p, At, e); // p = At e
    time += update(x, x, 1, p); // x := max(0, x + p)
    k++;
}

