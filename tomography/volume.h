#pragma once
#include "image.h"

generic struct VolumeT;

struct Volume {
    Volume(){}
    Volume(uint sampleSize, int3 sampleCount) : sampleSize(sampleSize), sampleCount(sampleCount), data(size()*sampleSize) { data.clear(0); }

    explicit operator bool() const { return (bool)data; }
    size_t size() const { return (size_t)sampleCount.x*sampleCount.y*sampleCount.z; }
    bool tiled() const { if(offsetX || offsetY || offsetZ) { assert(offsetX && offsetY && offsetZ); return true; } else return false; }

    bool contains(int3 position) const { return position>=int3(0) && position<sampleCount; }
    size_t index(uint x, uint y, uint z) const {
        assert_(x<uint(sampleCount.x) && y<uint(sampleCount.y) && z<uint(sampleCount.z));
        return tiled() ? offsetX[x]+offsetY[y]+offsetZ[z] : uint64(z)*uint64(sampleCount.x*sampleCount.y)+y*sampleCount.x+x;
    }
    size_t index(int3 position) const { return index(position.x, position.y, position.z); }

    generic operator const VolumeT<T>&() const { assert_(sampleSize==sizeof(T),sampleSize,sizeof(T)); return *(const VolumeT<T>*)this; }
    generic operator VolumeT<T>&() { assert_(sampleSize==sizeof(T),sampleSize,sizeof(T)); return *(VolumeT<T>*)this; }

    uint sampleSize=0; // Sample integer size (in bytes)
    int3 sampleCount = 0; // Sample counts (along each dimensions)
    buffer<byte> data; // Samples in Z-Order
    /*static*/ buffer<int32> offsetX, offsetY, offsetZ; // Offset lookup tables for Z-ordered volumes (max 16GB) //FIXME: share
};

generic struct VolumeT : Volume {
    VolumeT() {}
    // Allocates a volume in private memory
    VolumeT(int3 sampleCount) : Volume(sizeof(T), sampleCount) { interleavedLookup(*this); assert_(tiled()); }
    VolumeT(uint sampleCount) : VolumeT(int3(sampleCount)) { assert_(tiled()); }

    operator const ref<T>() const { assert(data.size==sizeof(T)*size()); return ref<T>((const T*)data.data, data.size/sizeof(T)); }
    operator const mref<T>() { assert(data.size==sizeof(T)*size()); return mref<T>((T*)data.data, data.size/sizeof(T)); }
    operator const T*() const { assert(data.size==sizeof(T)*size(), data.size, sizeof(T)*size()); return (const T*)data.data; }
    operator T*() const { assert(data.size==sizeof(T)*size(), data.size, sizeof(T)*size()); return (T*)data.data; }
    mref<T> slice(size_t pos, size_t size) { return operator const mref<T>().slice(pos, size); }
    template<Type... Args> void clear(Args... args) { operator const mref<T>().clear(args...); }
    T operator()(uint x, uint y, uint z) const { return ((const T*)data.data)[index(x,y,z)]; }
    T& operator()(uint x, uint y, uint z) { return ((T*)data.data)[index(x,y,z)]; }
};

typedef VolumeT<float> VolumeF;

/// Generates lookup tables for tiled volume data access
void interleavedLookup(Volume& target);
/// Interleaves 3 coordinates
uint64 zOrder(int3 coordinates);
/// Uninterleaves 2 coordinates
int2 zOrder2(uint64 index);
/// Uninterleaves 3 coordinates
int3 zOrder3(uint64 index);
