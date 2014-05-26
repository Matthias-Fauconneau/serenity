#pragma once
#include "volume.h"
#include "project.h"
#include "thread.h"

inline float SSQ(const VolumeF& x) {
    const float* xData = x;
    float SSQ[coreCount] = {};
    chunk_parallel(x.size(), [&](uint id, uint offset, uint size) {
        float accumulator = 0;
        for(uint i: range(offset,offset+size)) accumulator += sq(xData[i]);
        SSQ[id] += accumulator;
    });
    return sum(SSQ);
}

inline float SSE(const VolumeF& a, const VolumeF& b) {
    assert_(a.size() == b.size());
    const float* aData = a; const float* bData = b;
    float SSE[coreCount] = {};
    chunk_parallel(a.size(), [&](uint id, uint offset, uint size) {
        float accumulator = 0;
        for(uint i: range(offset,offset+size)) accumulator += sq(aData[i] - bData[i]);
        SSE[id] += accumulator;
    });
    return sum(SSE);
}

inline float SSQ(const ref<ImageF>& images) {
    float SSQ[coreCount] = {};
    parallel(images, [&](uint id, const ImageF& image) {
        float accumulator = 0;
        for(uint i: range(image.data.size)) accumulator += sq(image.data[i]);
        SSQ[id] += accumulator;
    });
    return sum(SSQ);
}

inline float SSE(const ref<ImageF>& A, const ref<ImageF>& B) {
    assert_(A.size == B.size);
    float SSE[coreCount] = {};
    parallel(A.size, [&](uint id, uint index) {
        float accumulator = 0;
        const ImageF& a = A[index];
        const ImageF& b = B[index];
        assert(a.data.size == b.data.size);
        for(uint i: range(a.data.size)) accumulator += sq(a.data[i] - b.data[i]);
        SSE[id] += accumulator;
    });
    return sum(SSE);
}

inline float SSQ(const VolumeF& volume, const ref<Projection>& projections) {
    float SSQ = 0;
    for(Projection projection: projections) {
        float accumulator = 0;
        ImageF image = ImageF(projection.imageSize);
        project(image, volume, projection);
        for(uint i: range(image.data.size)) accumulator += sq(image.data[i]);
        SSQ += accumulator;
    }
    return SSQ;
}

inline float SSE(const VolumeF& volume, const ref<Projection>& projections, const ref<ImageF>& references) {
    float SSE = 0;
    for(uint projectionIndex: range(projections.size)) {
        float accumulator = 0;
        ImageF image = ImageF(projections[projectionIndex].imageSize);
        project(image, volume, projections[projectionIndex]);
        const ImageF& reference = references[projectionIndex];
        assert(image.data.size == reference.data.size);
        for(uint i: range(reference.data.size)) accumulator += sq(image.data[i] - reference.data[i]);
        SSE += accumulator;
    }
    return SSE;
}
