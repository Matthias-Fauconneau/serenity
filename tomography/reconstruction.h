#pragma once
#include "matrix.h"
#include "volume.h"
#include "time.h"
#include "project.h"

struct Reconstruction {
    uint k = 0;
    Time totalTime;
    VolumeF x;
    Reconstruction(uint N) : x(N) {}
    virtual ~Reconstruction() {}
    virtual void initialize(const ref<Projection>& projections, const ref<ImageF>& images) abstract;
    virtual bool step(const ref<Projection>& projections, const ref<ImageF>& images) abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.totalTime < b.totalTime; }
inline String str(const Reconstruction& r) { return str(r.k, r.totalTime); }
