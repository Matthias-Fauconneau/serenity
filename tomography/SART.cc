#include "SART.h"
#include "operators.h"
#include "time.h"

SART::SART(const Projection& A, ImageArray&& attenuation, const uint subsetSize) : SubsetReconstruction(A, attenuation, subsetSize, "SART"_), AAti(subsets.size), Ax(subsets[0].b.size,"Ax"_), Atr(x.size,"Atr"_) {
    ImageArray i (Ax.size, "i"_, 1.f);
    log_("SART::AAti ["_+str(subsets.size)+"]... "_); Time time;
    for(uint subsetIndex: range(subsets.size)) {
        Subset& subset = subsets[subsetIndex];
        CLVolume Ati (x.size, "Ati"_);
        backproject(Ati, subset.At, i); // Backprojects identity projections
        AAti << ImageArray(subset.b.size, "AAti"_);
        project(AAti[subsetIndex], A, Ati, subsetIndex, subsetSize, subsetCount); // Projects coefficents volume
    }
    log(time);
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
