#pragma once
#include "image.h"

struct VolumeF {
    VolumeF(){}
    VolumeF(int3 sampleCount) : sampleCount(sampleCount), data(size()) { data.clear(0); }
    explicit operator bool() const { return (bool)data; }
    size_t size() const { return (size_t)sampleCount.x*sampleCount.y*sampleCount.z; }
    operator float*() const { return (float*)data.data; }

    int3 sampleCount = 0; // Sample counts (along each dimensions)
    buffer<float> data; // Samples in Z-Order
};
