#include "MLTR.h"
#include "operators.h"
#include "time.h"

MLTR::MLTR(int3 size, const ImageArray& b) : SubsetReconstruction(size, b), Ai(subsets.size), Ax(subsets[0].b.size), r(Ax.size), Atr(size), w(Ax.size), Atw(size)  {
    CLVolume i = cylinder(size);
    for(uint subsetIndex: range(subsets.size)) {
        log_(str(subsetIndex)+" "_);
        Subset& subset = subsets[subsetIndex];
        const ImageArray& b = subset.b;
        new (&Ai[subsetIndex]) ImageArray(b.size);
        project(Ai[subsetIndex], i, subsetIndex*subsetSize, projectionCount); // Ai = A i
    }
}

void MLTR::step() {
    time += project(Ax, x, subsetIndex*subsetSize, projectionCount); // Ax = A x
    //const ImageArray& e = Ax; // In-place: difference
    time += diffexp(r, Ax, subsets[subsetIndex].b); // r = exp(-Ax) - exp(-b) FIXME: precompute exp(-b)
    time += backproject(Atr, subsets[subsetIndex].At, r); // Atr = At r
    time += mulexp(w, Ai[subsetIndex], Ax); // w = Ai exp(-Ax) FIXME: compute exp(-Ax) once
    time += backproject(Atw, subsets[subsetIndex].At, w); // Atw = At w
    time += adddiv(x, x, Atr, Atw);
    subsetIndex = (subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
    k++;
}

