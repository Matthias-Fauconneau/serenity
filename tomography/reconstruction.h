#pragma once
#include "project.h"
#include "time.h"

struct Reconstruction {
    VolumeF x;

    int k = 0;
    Time totalTime;

    ProjectionArray At;
    const ImageArray& b;

    Reconstruction(int3 size, const ImageArray& b) : x(size), At(size, b.size), b(b) {}
    virtual ~Reconstruction() {}
    virtual bool step() abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.totalTime < b.totalTime; }
inline String str(const Reconstruction& r) { return str(r.k, r.totalTime); }
