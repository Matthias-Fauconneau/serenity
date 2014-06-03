#pragma once
#include "volume.h"
#include "project.h"
#include "time.h"

struct Reconstruction {
    Folder folder {"Results"_, home()};
    String name;
    VolumeF x;

    int k = 0;
    Time totalTime;
    Reconstruction(int3 size, int3 projectionSize, string label) : name(label+"-"_+strx(projectionSize)+"."_+strx(size)) {
        Map map;
#if 0
        for(string name: folder.list()) { TextData s(name); if(s.match(this->name+"."_)) { int k = s.integer(); if(k>0) this->k=k; } }
        if(k>0) {
            File file (name+"."_+str(k), folder, ReadOnly/*Write*/);
            map = Map(file, Map::Prot(Map::Read/*|Map::Write*/));
        } else
#else
        if(existsFile(name, folder)) removeFile(name, folder);
#endif
        {
            File file (name, folder, Flags(ReadWrite|Create));
            file.resize(size.x*size.y*size.z*sizeof(float));
            map = Map(file, Map::Prot(Map::Read|Map::Write));
        }
        x = VolumeF(size, cast<float>(ref<byte>(map)));
    }
    virtual ~Reconstruction() {}
    virtual bool step() abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.totalTime < b.totalTime; }
inline String str(const Reconstruction& r) { return str(r.k, r.totalTime); }
