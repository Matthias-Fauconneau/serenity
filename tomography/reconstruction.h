#pragma once
#include "project.h"

struct Reconstruction {
    Projection A; // Projection operator
    CLVolume x;
    int k = 0;
    uint64 time = 0;
    float SSE = inf;

    Reconstruction(const Projection& A, string name) : A(A), x(cylinder(VolumeF(A.volumeSize, name), 1.f/A.volumeSize.x)) { assert_(x.size.x==x.size.y); }
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

    SubsetReconstruction(const Projection& projection, const ImageArray& b, const uint subsetSize, string name) : Reconstruction(projection, name), subsetSize(subsetSize), subsetCount(projection.count/subsetSize) { // FIXME
        assert_(subsetCount*subsetSize == projection.count);
        subsets = buffer<Subset>(subsetCount);
        for(uint subsetIndex: range(subsetCount)) {
            Subset& subset = subsets[subsetIndex];
            uint startIndex = subsetIndex*subsetSize, endIndex = startIndex+subsetSize;
            CLVolume subsetB = int3(b.size.xy(),subsetSize);
            for(uint index: range(subsetSize)) copy(b, subsetB, int3(0,0,interleave(subsetSize, subsetCount, startIndex+index)), int3(0,0,index), int3(b.size.xy(),1));
            new (&subset) Subset{ apply(range(startIndex, endIndex), [&](uint index){ return projection.worldToView(interleave(subsetSize, subsetCount, index)); }), move(subsetB)};
        }
    }
};
