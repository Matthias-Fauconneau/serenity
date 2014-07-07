#include "SART.h"
#include "operators.h"
#include "time.h"

// Simultaneous iterative algebraic reconstruction technique
SART::SART(const Projection& A, const ImageArray& attenuation, const uint subsetSize) : SubsetReconstruction(A, attenuation, subsetSize, "SART"_), AAti(subsets.size), Ax(subsets[0].b.size), Atr(x.size) {
    ImageArray i (Ax.size, 1.f, "i"_);
    log_("SART: AAti... "_);
    for(uint subsetIndex: range(subsets.size)) {
        Subset& subset = subsets[subsetIndex];
        CLVolume Ati (x.size);
        backproject(Ati, subset.At, i); // Backprojects identity projections
        AAti << ImageArray(subset.b.size);
        project(AAti[subsetIndex], A, Ati, subsetIndex, subsetSize, subsetCount); // Projects coefficents volume
    }
    log("Done");
}

void SART::step() {
    uint subsetIndex = shuffle[this->subsetIndex];
    time += project(Ax, A, x, subsetIndex, subsetSize, subsetCount); // Ax = A x
    const ImageArray& r = Ax; // In-place: residual
    time += divdiff(r, subsets[subsetIndex].b, Ax, AAti[subsetIndex]); // r = ( b - Ax ) / A At i
    time += backproject(Atr, subsets[subsetIndex].At, r); // Atr = At r
    time += maxadd(x, x, 1, Atr); // x := max(0, x + At r)
    this->subsetIndex = (this->subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
}

