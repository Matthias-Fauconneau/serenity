#pragma once
#include "image.h"
#include "file.h"

struct VolumeF {
    VolumeF() {}
    VolumeF(int3 size, string name=""_) : size(size), data(size.z*size.y*size.x), name(copy(String(name))) { assert(name.size <= 16); }
    VolumeF(int3 size, float value, string name) : size(size), data(size.z*size.y*size.x, size.z*size.y*size.x, value), name(copy(String(name))) { assert(name.size <= 16); }
    VolumeF(int3 size, buffer<float>&& data, string name) : size(size), data(move(data)), name(copy(String(name))) { assert_(this->data.size == (size_t)size.x*size.y*size.z); }
    inline float& operator()(uint x, uint y, uint z) const {assert_(x<uint(size.x) && y<uint(size.y) && z<uint(size.z), x,y,z, size); return data[(size_t)z*size.y*size.x+y*size.x+x]; }

    int3 size = 0;
    buffer<float> data;
    Map map;
    String name;
};

inline VolumeF sub(const VolumeF& source, int3 origin, int3 size) {
    assert_(origin.xy() == int2(0) && size.xy() == source.size.xy() && origin.z >=0 && origin.z+size.z <= source.size.z);
    return VolumeF(size, buffer<float>(source.data.slice(origin.z*size.y*size.x,size.z*size.y*size.x)), source.name);
}

inline ImageF slice(const VolumeF& volume, size_t index /* Z slice or projection*/) {
    int3 size = volume.size;
    assert_(index < size_t(size.z), index);
    return ImageF(buffer<float>(volume.data.slice(index*size.y*size.x,size.y*size.x)), size.xy());
}

inline float SSE(const ref<float>& a, const ref<float>& b) { assert_(a.size==b.size); double SSE=0; for(uint i: range(a.size)) SSE+=sq(a[i]-b[i]); return SSE; }
inline float dot(const ref<float>& a, const ref<float>& b) { assert_(a.size==b.size); double dot=0; for(uint i: range(a.size)) dot+=a[i]*b[i]; return dot; }

inline float max(const VolumeF& x) { return max(x.data); }
inline float dot(const VolumeF& a, const VolumeF& b) { assert_(a.size==b.size); return dot(a.data, b.data); }
inline float sq(const VolumeF& x) { return dot(x,x); }
inline float SSE(const VolumeF& a, const VolumeF& b) { assert_(a.size==b.size); return SSE(a.data, b.data); }
inline void scale(VolumeF& x, float factor) { scale(x.data, factor); }

inline VolumeF cylinder(VolumeF&& target, const float value=1) {
    int3 size = target.size;
    const float2 center = float2(size.xy()-int2(1))/2.f;
    const float radiusSq = sq(center.x);
    for(uint z: range(size.z)) for(uint y: range(size.y)) for(uint x: range(size.x)) target(x,y,z) = sq(float2(x,y)-center)<=radiusSq ? value : 0;
    return move(target);
}
