#include "OSTR.h"
#include "operators.h"
#include "time.h"

OSTR::OSTR(int3 size, const ImageArray& b) : SubsetReconstruction(size, b), b0(b.size.xy(), 0.f), b1(b.size.xy(), 1.f), Ax(subsets[0].b.size), Aic(Ax.size), h(Ax.size), L(size), d(size) {
    Ai = buffer<ImageArray>(subsets.size);
    CLVolume i = cylinder(size);
    for(uint subsetIndex: range(subsets.size)) {
        log("Ai", subsetIndex);
        Subset& subset = subsets[subsetIndex];
        const ImageArray& b = subset.b;
        new (&Ai[subsetIndex]) ImageArray(b.size);
        project(Ai[subsetIndex], i, subsetIndex*subsetSize, projectionCount); // Projects coefficents volume
        const ProjectionArray& At = subset.At;
        backproject(x, At, b/*, subsetIndex==0?0:1*/); // FIXME: Initial estimate
    }
}

bool isNumber(const ref<float>& data) { for(float v: data) if(!isNumber(v)) return false; return true; }
#define assertVolume(x) { assert_(isNumber(x.read(x.size).data)); }

void OSTR::step() {
    assertVolume(x);
    time += project(Ax, x, subsetIndex*subsetSize, projectionCount); // Ax = A x
    assertVolume(subsets[subsetIndex].b); assertVolume(Ax);
    time += estimate(h, subsets[subsetIndex].b, b0, b1, Ax); // h = (b / (b0·exp(-Ax)+b1) - 1) b0·exp(-Ax)
    assertVolume(h);
    //const ImageArray& Aic = Ax; // In-place
    time += curvature(Aic, subsets[subsetIndex].b, b0, b1, Ax, Ai[subsetIndex], h); // - Ai c (Ax, h(b,b0,b1,Ax))
    time += backproject(L, subsets[subsetIndex].At, h); // L = At h
    time += backproject(d, subsets[subsetIndex].At, Aic); // d = At Ai c
    time += maximize(x, x, L, d);
    subsetIndex = (subsetIndex+1)%subsetCount; // Ordered subsets (FIXME: better scheduler)
    k++;
}

