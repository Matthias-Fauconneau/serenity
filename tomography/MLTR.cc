#include "MLTR.h"
#include "operators.h"
#include "time.h"

MLTR::MLTR(const Projection& projection, const ImageArray& intensity, const uint subsetSize) : SubsetReconstruction(projection, intensity, subsetSize, "MLTR"_, 1), Ai(subsets.size), Ax(subsets[0].b.size), r(Ax.size), Atr(x.size), Atw(x.size) {
    CLVolume i = cylinder(VolumeF(x.size,"i"_));
    log_("MLTR: Ai... "_); Time time;
    for(uint subsetIndex: range(subsets.size)) {
        Subset& subset = subsets[subsetIndex];
        const ImageArray& b = subset.b;
        Ai << ImageArray(b.size);
        project(Ai[subsetIndex], A, i, subsetIndex, subsetSize, subsetCount); // Ai = A i
    }
    log(time);
}

void MLTR::step() {
    uint subsetIndex = shuffle[this->subsetIndex];
    time += project(Ax, A, x, subsetIndex, subsetSize, subsetCount); // Ax = A x
    time += diffexp(r, Ax, subsets[subsetIndex].b); // r = exp(-Ax) - b
    time += backproject(Atr, subsets[subsetIndex].At, r); // Atr = At r
    const ImageArray& w = r; // Reuse for normalization
    time += mulexp(w, Ai[subsetIndex], Ax); // w = Ai exp(-Ax) FIXME: compute exp(-Ax) once
    time += backproject(Atw, subsets[subsetIndex].At, w); // Atw = At w
    time += adddiv(x, x, Atr, Atw);
    this->subsetIndex = (this->subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
}
