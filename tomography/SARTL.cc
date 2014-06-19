#include "SARTL.h"
#include "operators.h"
#include "time.h"

inline CLVolume ln(const CLVolume& a) { CLVolume y(a.size); ln(y, a); return y; }

// Simultaneous iterative algebraic reconstruction technique
SARTL::SARTL(int3 size, const ImageArray& b, const uint subsetSize) : SubsetReconstruction(size, ln(b), subsetSize, "SARTL"_), AAti(subsets.size), Ax(subsets[0].b.size), Atr(size), lnX(ln(x)) {
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

void SARTL::step() {
    time += project(Ax, lnX, subsetIndex*subsetSize, projectionCount); // Ax = A x
    const ImageArray& r = Ax; // In-place: residual
    time += divdiff(r, subsets[subsetIndex].b, Ax, AAti[subsetIndex]); // r = ( b - Ax ) / A At i
    time += backproject(Atr, subsets[subsetIndex].At, r); // Atr = At r
    time += add(lnX, 1, lnX, 1, Atr); // x := x + Atr
    exp(x, lnX);
    subsetIndex = (subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
    k++;
}

