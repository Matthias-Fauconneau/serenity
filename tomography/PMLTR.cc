#include "PMLTR.h"
#include "operators.h"
#include "time.h"

PMLTR::PMLTR(const Projection& projection, ImageArray&& intensity, const uint subsetSize) : SubsetReconstruction(projection, intensity, subsetSize, "P-MLTR"_), Ax(subsets[0].b.size,"Ax"_), r(Ax.size,"r"_), Atr(x.size,"Atr"_), Atw(x.size,"Atw"_) {}

void PMLTR::step() {
    time += project(Ax, A, x, subsetIndex, subsetSize, subsetCount); // Ax = A x
    time += diffexp(r, Ax, subsets[subsetIndex].b); // r = exp(-Ax) - exp(-b) FIXME: precompute exp(-b)
    time += backproject(Atr, subsets[subsetIndex].At, r); // Atr = At r
    const ImageArray& w = r; // Reuse for normalization
    time += mulexp(w, Ax, Ax); // w = Ax exp(-Ax) FIXME: compute exp(-Ax) once
    time += backproject(Atw, subsets[subsetIndex].At, w); // Atw = At w
    time += muldiv(x, x, Atr, Atw);
    subsetIndex = (subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
}
