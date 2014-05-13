#pragma once
#include "matrix.h"
#include "volume.h"
#include "time.h"
#include "project.h"

struct Reconstruction {
    Folder folder {"Results"_, home()};
    String name;
    Map map;
    VolumeF x;

    int k = -1;
    Time totalTime;
    Reconstruction(int3 N, uint projectionCount, string label) : name(label+"-"_+dec(projectionCount)+"."_+str(N.x)+"x"_+str(N.y)+"x"_+str(N.z)) {
        x.sampleCount = N;
        File file;
#if 0
        for(string name: folder.list()) { TextData s(name); if(s.match(this->name+"."_)) { int k = s.integer(); if(k>0) this->k=k; } }
        if(k>0) {
            file = File(name+"."_+str(k), folder, ReadOnly/*Write*/);
            map = Map(file, Map::Prot(Map::Read/*|Map::Write*/));
        } else
#endif
        {
            file = File(name, folder, Flags(ReadWrite|Create));
            file.resize(x.size()*sizeof(float));
            map = Map(file, Map::Prot(Map::Read|Map::Write));
        }
        x.data = buffer<float>(cast<float>(ref<byte>(map)));
        assert_(x.data.size == x.size());
    }
    virtual ~Reconstruction() {}
    virtual void initialize(const ref<Projection>& projections, const ref<ImageF>& images) abstract;
    virtual bool step(const ref<Projection>& projections, const ref<ImageF>& images) abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.totalTime < b.totalTime; }
inline String str(const Reconstruction& r) { return str(r.k, r.totalTime); }
