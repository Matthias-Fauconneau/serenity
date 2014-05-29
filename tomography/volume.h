#pragma once
#include "image.h"

struct VolumeF {
    VolumeF(){}
    VolumeF(int3 sampleCount) : sampleCount(sampleCount), data(size()) { data.clear(0); }
    VolumeF(int3 sampleCount, const ref<float>& data) : sampleCount(sampleCount), data(data) { assert_(data.size == size()); }
    explicit operator bool() const { return (bool)data; }
    size_t size() const { return (size_t)sampleCount.x*sampleCount.y*sampleCount.z; }
    operator float*() const { return (float*)data.data; }

    int3 sampleCount = 0; // Sample counts (along each dimensions) //FIXME: -> dimensions
    buffer<float> data; // Samples in Z-Order
};

inline ImageF slice(const VolumeF& volume, uint z) {
    int3 size = volume.sampleCount;
    assert_(z < uint(size.z), z, size.z);
    return ImageF(buffer<float>(volume.data.slice(z*size.y*size.x,size.y*size.x)), size.x, size.y);
}
