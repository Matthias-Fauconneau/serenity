#pragma once
#include "volume.h"
#include "project.h"
#include "time.h"

struct Reconstruction {
    Folder folder {"Results"_, home()};
    //String name;
    VolumeF x;

    int k = 0;
    Time totalTime;
    Reconstruction(int3 size, int3 /*projectionSize*/, string /*label*/) : /*name(label+"-"_+strx(projectionSize)+"."_+strx(size)),*/ x(size) {}
    virtual ~Reconstruction() {}
    virtual bool step() abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.totalTime < b.totalTime; }
inline String str(const Reconstruction& r) { return str(r.k, r.totalTime); }
