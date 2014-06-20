#include "MLEM.h"
#include "operators.h"
#include "time.h"

// Simultaneous iterative algebraic reconstruction technique
MLEM::MLEM(int3 size, const ImageArray& b, const uint subsetSize) : SubsetReconstruction(size, negln(b), subsetSize, "MLEM"_), Ati(subsets.size), Ax(subsets[0].b.size), Atr(size) {
    ImageArray i (Ax.size, 1.f);
    for(uint subsetIndex: range(subsets.size)) {
        Subset& subset = subsets[subsetIndex];
        const ProjectionArray& At = subset.At;
        new (&Ati[subsetIndex]) CLVolume(size);
        backproject(Ati[subsetIndex], At, i); // Backprojects identity projections
    }
}

// FIXME: <=> SART step on log (would have better precision)
void MLEM::step() {
    time += project(Ax, x, subsetIndex, subsetSize, subsetCount); // Ax = A x
    const ImageArray& r = Ax; // In-place: residual

    time += div(r, subsets[subsetIndex].b, Ax); // r = b / Ax
    time += backproject(Atr, subsets[subsetIndex].At, r); // Atr = At r
    time += muldiv(x, x, Atr, Ati[subsetIndex]); // x := x * Atr / Ati

    subsetIndex = (subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
    k++;
}
