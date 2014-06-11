#pragma once
#include "project.h"

struct Reconstruction {
    CLVolume x;

    int k = 0;
    uint64 totalTime = 0;

    ProjectionArray At;
    const ImageArray& b;

    Reconstruction(int3 size, const ImageArray& b) : x(size), At(size, b.size), b(b) {}
    virtual ~Reconstruction() {}
    virtual void step() abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.totalTime < b.totalTime; }
inline String str(const Reconstruction& r) { return str(r.k, r.totalTime); }
