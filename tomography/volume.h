#pragma once
#include "image.h"
#include "simd.h"

struct VolumeF {
    VolumeF(){}
    VolumeF(int3 sampleCount) : sampleCount(sampleCount), data(size()) { data.clear(0); }
    VolumeF(int3 sampleCount, ref<float> data) : sampleCount(sampleCount), data(data) { assert_(data.size == size()); }
    explicit operator bool() const { return (bool)data; }
    size_t size() const { return (size_t)sampleCount.x*sampleCount.y*sampleCount.z; }
    operator float*() const { return (float*)data.data; }

    int3 sampleCount = 0; // Sample counts (along each dimensions)
    buffer<float> data; // Samples in Z-Order
};

struct CylinderVolume {
    CylinderVolume(const VolumeF& volume) {
        assert(volume.sampleCount.x == volume.sampleCount.y);
        const float radius = float(volume.sampleCount.x-1)/2, halfHeight = float(volume.sampleCount.z-1)/2; // Cylinder parameters (N-1 [domain size] - epsilon (Prevents out of bounds on exact $-1 (ALT: extend offsetZ by one row (gather anything and multiply by 0))
        capZ = (v4sf){halfHeight, halfHeight, -halfHeight, -halfHeight};
        radiusR0R0 = (v4sf){radius*radius, 0, radius*radius, 0};
        radiusSqHeight = (v4sf){radius*radius, radius*radius, halfHeight, halfHeight};
        dataOrigin = float4(float(volume.sampleCount.x-1)/2, float(volume.sampleCount.y-1)/2, float(volume.sampleCount.z-1)/2, 0);
        stride = (v4si){1, int(volume.sampleCount.x), int(volume.sampleCount.x*volume.sampleCount.y), 0};
        offset = (v4si){0, int(volume.sampleCount.x), int(volume.sampleCount.x*volume.sampleCount.y), int(volume.sampleCount.x*volume.sampleCount.y+volume.sampleCount.x)};
    }

    // Precomputed parameters
    v4sf capZ;
    v4sf radiusR0R0;
    v4sf radiusSqHeight;
    v4sf dataOrigin;
    v4si stride;
    v4si offset;
};
