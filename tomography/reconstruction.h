#pragma once
#include "project.h"

struct Reconstruction {
    CLVolume x;
    const uint projectionCount;
    int k = 0;
    uint64 time = 0;
    float SSE = inf;

    Reconstruction(int3 size, const ImageArray& b, string name) : x(cylinder(VolumeF(size, name), 1.f/size.x)), projectionCount(b.size.z) { assert_(size.x==size.y); }
    virtual ~Reconstruction() {}
    virtual void step() abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.time < b.time; }

struct SubsetReconstruction : Reconstruction {
    const uint subsetSize, subsetCount;
    struct Subset {
        ProjectionArray At;
        ImageArray b;
    };
    buffer<Subset> subsets;

    uint subsetIndex = 0;

    SubsetReconstruction(int3 size, const ImageArray& b, const uint subsetSize, string name) : Reconstruction(size, b, name), subsetSize(subsetSize), subsetCount(projectionCount/subsetSize) { // FIXME
        assert_(subsetCount*subsetSize == projectionCount);
        subsets = buffer<Subset>(subsetCount);
        for(uint subsetIndex: range(subsetCount)) {
            Subset& subset = subsets[subsetIndex];
            uint startIndex = subsetIndex*subsetSize, endIndex = startIndex+subsetSize;
            CLVolume subsetB = int3(b.size.xy(),subsetSize);
            for(uint index: range(subsetSize)) copy(b, subsetB, int3(0,0,interleave(subsetSize, subsetCount, startIndex+index)), int3(0,0,index), int3(b.size.xy(),1));
            new (&subset) Subset{ apply(range(startIndex, endIndex), [&](uint index){ return Projection(size, b.size, interleave(subsetSize, subsetCount, index)).worldToView; }), move(subsetB)};
        }
    }
};
