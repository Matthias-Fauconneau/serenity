#pragma once
#include "project.h"

struct Reconstruction {
    Projection A; // Projection operator
    CLVolume x;
    uint k = 0;
    uint64 time = 0;
    float SSE = inf;
    float bestSSE = inf;
    int divergent = 0; // Divergent iterations
    uint64 stopTime = 0;

    Reconstruction(const Projection& A, string name) : A(A), x(cylinder(VolumeF(A.volumeSize, 0, name), /*1.f/A.volumeSize.x*/0)) { assert_(x.size.x==x.size.y); }
    virtual ~Reconstruction() {}
    virtual void step() abstract;
};
inline bool operator <(const Reconstruction& a, const Reconstruction& b) { return a.time < b.time; }

inline buffer<uint> shuffleSequence(uint size) {
    buffer<uint> seq(size);
    Random random;
    for(uint i: range(size)) seq[i] = i;
    for(uint i=size-1; i>0; i--) swap(seq[i], seq[random%(i+1)]);
    return seq;
}

struct SubsetReconstruction : Reconstruction {
    const uint subsetSize, subsetCount;
    struct Subset {
        ProjectionArray At;
        ImageArray b;
    };
    buffer<Subset> subsets;

    uint subsetIndex = 0;
    buffer<uint> shuffle = shuffleSequence(subsetCount);

    SubsetReconstruction(const Projection& A, const ImageArray& b, const uint subsetSize, string name) : Reconstruction(A, name), subsetSize(subsetSize), subsetCount(A.count/subsetSize) {
        assert_(subsetCount*subsetSize == A.count);
        subsets = buffer<Subset>(subsetCount);
        for(uint subsetIndex: range(subsetCount)) {
            /*CLVolume subsetB = int3(b.size.xy(), subsetSize);
            for(uint index: range(subsetSize)) {
                log(interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index),"->", subsetIndex, index);
                copy(b, subsetB, int3(0,0,interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)), int3(0,0,index), int3(b.size.xy(),1));
            }
            new (subsets+subsetIndex) Subset{ apply(subsetSize, [&](uint index){ return A.worldToDevice(interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)); }), move(subsetB)};*/
            /*new (subsets+subsetIndex) Subset{ apply(subsetSize, [&](uint index){ return A.worldToDevice(interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)); }), int3(b.size.xy(), subsetSize)};
            for(uint index: range(subsetSize)) {
                log(interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index),"->", subsetIndex, index);
                copy(b, subsets[subsetIndex].b, int3(0,0,interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)), int3(0,0,index), int3(b.size.xy(),1));
            }*/
            VolumeF source(b.size,"b"_); b.read(source);
            VolumeF target(int3(b.size.xy(), subsetSize), "b"_);
            for(uint index: range(subsetSize)) {
                log(interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index),"->", subsetIndex, index);
                copy(slice(target,index).data,slice(source,interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)).data);
            }
            new (subsets+subsetIndex) Subset{ apply(subsetSize, [&](uint index){ return A.worldToDevice(interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index)); }), CLVolume(target.size, target.data) };
        }
    }
};
