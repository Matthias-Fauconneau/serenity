#include "SART.h"
#include "operators.h"
#include "time.h"

// Simultaneous iterative algebraic reconstruction technique
SART::SART(int3 size, const ImageArray& b, const uint subsetSize) : SubsetReconstruction(size, b, subsetSize, "SART"_), AAti(subsets.size), Ax(subsets[0].b.size), Atr(size) {
    ImageArray i (Ax.size, 1.f);
    for(uint subsetIndex: range(subsets.size)) {
        Subset& subset = subsets[subsetIndex];
        const ProjectionArray& At = subset.At;
        CLVolume Ati (size);
        backproject(Ati, At, i); // Backprojects identity projections
        const ImageArray& b = subset.b;
        new (&AAti[subsetIndex]) ImageArray(b.size);
        project(AAti[subsetIndex], Ati, subsetIndex*subsetSize, projectionCount); // Projects coefficents volume
    }
}

void SART::step() {
    time += project(Ax, x, subsetIndex*subsetSize, projectionCount); // Ax = A x
    const ImageArray& r = Ax; // In-place: residual
    time += divdiff(r, subsets[subsetIndex].b, Ax, AAti[subsetIndex]); // r = ( b - Ax ) / A At i
    time += backproject(Atr, subsets[subsetIndex].At, r); // Atr = At r
    time += maxadd(x, x, 1, Atr); // x := max(0, x + At r)
    subsetIndex = (subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
    k++;
}

