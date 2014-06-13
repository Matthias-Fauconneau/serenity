#pragma once
#include "project.h"

struct Reconstruction {
    CLVolume x;

    int k = 0;
    uint64 time = 0;

    struct Subset {
        ProjectionArray At;
        ImageArray b;
    };
    buffer<Subset> subsets;
    const uint projectionCount, subsetSize, subsetCount;

    uint subsetIndex = 0;

    Reconstruction(int3 size, const ImageArray& b) : x(size), projectionCount(b.size.z), subsetSize(round(sqrt(float(projectionCount)))), subsetCount(round(sqrt(float(projectionCount)))) { // FIXME
        assert_(subsetCount*subsetSize == projectionCount);
        subsets = buffer<Subset>(subsetCount);
        for(uint subsetIndex: range(subsetCount)) {
            Subset& subset = subsets[subsetIndex];
            uint startIndex = subsetIndex*subsetSize, endIndex = startIndex+subsetSize;
            CLVolume subsetB = int3(b.size.xy(),subsetSize);
            copy(subsetB, b, int3(0,0,startIndex));
            new (&subset) Subset{ apply(range(startIndex, endIndex), [&](uint index){ return Projection(size, b.size, index).worldToView; }), move(subsetB)};
        }
    }
    virtual ~Reconstruction() {}
    virtual void step() abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.time < b.time; }
