#include "OSTR.h"
#include "operators.h"
#include "time.h"

OSTR::OSTR(int3 size, const ImageArray& b) : SubsetReconstruction(size, b), b0(b.size.xy(), 0.f), b1(b.size.xy(), 1.f), Ax(subsets[0].b.size), h(Ax.size), L(size), d(size) {
    Ai = buffer<ImageArray>(subsets.size);
    CLVolume i = cylinder(size);
    for(uint subsetIndex: range(subsets.size)) {
        log("Ai", subsetIndex);
        Subset& subset = subsets[subsetIndex];
        const ImageArray& b = subset.b;
        new (&Ai[subsetIndex]) ImageArray(b.size);
        project(Ai[subsetIndex], i, subsetIndex*subsetSize, projectionCount); // Projects coefficents volume
    }
}

void OSTR::step() {
    time += project(Ax, x, subsetIndex*subsetSize, projectionCount); // Ax = A x
    time += estimate(h, subsets[subsetIndex].b, b0, b1, Ax); // h = (b / (b0·exp(-Ax)+b1) - 1) b0·exp(-Ax)
    const ImageArray& Aic = Ax; // In-place
    time += curvature(Aic, subsets[subsetIndex].b, b0, b1, Ax, Ai[subsetIndex], h); // Ai c (Ax, h(b,b0,b1,Ax))
    time += backproject(L, subsets[subsetIndex].At, h); // L = At h
    time += backproject(d, subsets[subsetIndex].At, Aic); // d = At Ai c
    time += maximize(x, x, L, d);
    subsetIndex = (subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
    k++;
}

