#pragma once
#include "image.h"

struct VolumeF {
    VolumeF(){}
    VolumeF(int3 sampleCount) : sampleCount(sampleCount), data(size()) { data.clear(0); }
    VolumeF(int3 sampleCount, const ref<float>& data) : sampleCount(sampleCount), data(data) { assert_(data.size == size()); }
    explicit operator bool() const { return (bool)data; }
    size_t size() const { return (size_t)sampleCount.x*sampleCount.y*sampleCount.z; }
    operator float*() const { return (float*)data.data; }

    int3 sampleCount = 0; // Sample counts (along each dimensions)
    buffer<float> data; // Samples in Z-Order
};

inline ImageF slice(const VolumeF& volume, uint z) {
    int3 size = volume.sampleCount;
    assert_(z < uint(size.z), z, size.z);
    return ImageF(buffer<float>(volume.data.slice(z*size.y*size.x,size.y*size.x)), size.x, size.y);
}

#if 0
struct CylinderVolume {
    CylinderVolume(const VolumeF& volume) {
        assert(volume.sampleCount.x == volume.sampleCount.y);
        float radius = float(volume.sampleCount.x-1)/2;
        float halfHeight = float(volume.sampleCount.z-1 -1/*FIXME*/ )/2; // Cylinder parameters (N-1 [domain size] - epsilon)
        capZ = (float2){halfHeight, -halfHeight};
        radiusSq = radius*radius;
        dataOrigin = {float(volume.sampleCount.x-1)/2, float(volume.sampleCount.y-1)/2, float(volume.sampleCount.z-1)/2};
        stride = {1, int(volume.sampleCount.x), int(volume.sampleCount.x*volume.sampleCount.y), 0};
        offset = {0, int(volume.sampleCount.x), int(volume.sampleCount.x*volume.sampleCount.y), int(volume.sampleCount.x*volume.sampleCount.y+volume.sampleCount.x)};
        size = volume.sampleCount; // Bound check
    }

    // Precomputed parameters
    float2 capZ; // ±height/2
    float radiusSq;
    float3 dataOrigin;
    int4 stride;
    int4 offset;
    int3 size; // Bound check
};
#endif
