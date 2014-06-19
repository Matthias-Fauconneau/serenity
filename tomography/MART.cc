#include "MART.h"
#include "operators.h"
#include "time.h"

// Simultaneous iterative algebraic reconstruction technique
MART::MART(int3 size, const ImageArray& b, const uint subsetSize) : SubsetReconstruction(size, b, subsetSize, "MART"_), AAti(subsets.size), Ax(subsets[0].b.size), Atr(size) {
    AAti = buffer<ImageArray>(subsets.size);
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

// FIXME: <=> SART step on log (would have better precision)
void MART::step() {
    time += project(Ax, x, subsetIndex*subsetSize, projectionCount); // Ax = A x
    const ImageArray& r = Ax; // In-place: residual

    time += divdiv(r, subsets[subsetIndex].b, Ax, AAti[subsetIndex]); // r = ( b / Ax ) / A At i
    time += backproject(Atr, subsets[subsetIndex].At, r); // Atr = At r
    time += mul(x, x, Atr); // x := x * Atr

    subsetIndex = (subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
    k++;
}

