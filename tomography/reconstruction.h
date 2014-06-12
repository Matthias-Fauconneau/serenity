#pragma once
#include "project.h"

struct Reconstruction {
    CLVolume x;

    int k = 0;
    uint64 totalTime = 0;

    ProjectionArray At;
    const ImageArray& b;

    Reconstruction(int3 size, const ImageArray& b) : x(size), At(apply(b.size.z, [&](uint index){ return Projection(size, b.size, index).worldToView; })), b(b) {}
    virtual ~Reconstruction() {}
    virtual void step() abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.k < b.k /*a.totalTime < b.totalTime*/; }
inline String str(const Reconstruction& r) { return str(r.k, r.totalTime); }
