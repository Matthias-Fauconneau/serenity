#include "algebraic.h"
#include "operators.h"
#include "time.h"

Algebraic::Algebraic(int3 size, const ImageArray& b) : SubsetReconstruction(size, b), Ax(subsets[0].b.size), p(size) {
    AAti = buffer<ImageArray>(subsets.size);
    ImageArray i (Ax.size, 1.f);
    for(uint subsetIndex: range(subsets.size)) {
        log("AAti", subsetIndex);
        Subset& subset = subsets[subsetIndex];
        const ProjectionArray& At = subset.At;
        CLVolume Ati (size);
        backproject(Ati, At, i); // Backprojects identity projections
        const ImageArray& b = subset.b;
        new (&AAti[subsetIndex]) ImageArray(b.size);
        project(AAti[subsetIndex], Ati, subsetIndex*subsetSize, projectionCount); // Projects coefficents volume
        //backproject(x, At, b, subsetIndex==0?0:1/*add*/); // FIXME: Initial estimate
    }
}

void Algebraic::step() {
    time += project(Ax, x, subsetIndex*subsetSize, projectionCount); // Ax = A x
    const ImageArray& e = Ax; // In-place
    time += delta(e, subsets[subsetIndex].b, Ax, AAti[subsetIndex]); // e = ( b - Ax ) / A At i
    time += backproject(p, subsets[subsetIndex].At, e); // p = At e
    time += update(x, x, 1, p); // x := max(0, x + p)
    subsetIndex = (subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
    k++;
}

