#pragma once
#include "image.h"
#include "file.h"

struct VolumeF {
    VolumeF(int3 size, float value=0, string name=""_) : size(size), data(size.z*size.y*size.x, size.z*size.y*size.x, value), name(name) { assert(name.size <= 16); }
    VolumeF(int3 size, buffer<float>&& data, string name=""_) : size(size), data(move(data)), name(name) { assert_(this->data.size == (size_t)size.x*size.y*size.z); }
    //VolumeF(const ref<float>& data) : VolumeF(round(pow(data.size,1./3)), data) {}
    VolumeF(int3 size, Map&& map, string name=""_) : VolumeF(size, buffer<float>((ref<float>)map), name) { this->map = move(map); }
    inline float& operator()(uint x, uint y, uint z) const {assert_(x<uint(size.x) && y<uint(size.y) && z<uint(size.z), x,y,z, size); return data[(size_t)z*size.y*size.x+y*size.x+x]; }

    int3 size = 0;
    buffer<float> data;
    Map map;
    string name;
};

inline ImageF slice(const VolumeF& volume, size_t index /* Z slice or projection*/) {
    int3 size = volume.size;
    assert_(index < size_t(size.z), index);
    return ImageF(buffer<float>(volume.data.slice(index*size.y*size.x,size.y*size.x)), int2(size.x,size.y));
}

inline float sum(const ref<float>& A) { double sum=0; for(float a: A) sum+=a; return sum; }
inline float mean(const ref<float>& A) { return sum(A) / A.size; }
inline float SSQ(const ref<float>& A) { double SSQ=0; for(float a: A) SSQ+=a*a; return SSQ; }
inline float SSE(const ref<float>& A, const ref<float>& B) { assert_(A.size==B.size); double SSE=0; for(uint i: range(A.size)) SSE+=sq(A[i]-B[i]); return SSE; }
inline float dotProduct(const ref<float>& A, const ref<float>& B) { assert_(A.size==B.size); double dot=0; for(uint i: range(A.size)) dot+=A[i]*B[i]; return dot; }
inline buffer<float> operator*(float a, const ref<float>& A) { buffer<float> P(A.size); for(uint i: range(A.size)) P[i] = a*A[i]; return P; }

inline float sum(const VolumeF& volume) { return sum(volume.data); }
inline float mean(const VolumeF& volume) { return mean(volume.data); }
inline float SSQ(const VolumeF& volume) { return SSQ(volume.data); }
inline float SSE(const VolumeF& A, const VolumeF& B) { assert_(A.size==B.size); return SSE(A.data, B.data); }
inline float dotProduct(const VolumeF& A, const VolumeF& B) { assert_(A.size==B.size); return dotProduct(A.data, B.data); }
inline VolumeF scale(VolumeF&& volume, float factor) { scale(volume.data, factor); return move(volume); }

//inline VolumeF normalize(VolumeF&& volume) { float sum = ::sum(volume); assert_(sum); return scale(move(volume), 1./sum); }
//inline VolumeF operator*(float a, const VolumeF& A) { return VolumeF(A.size, a*A.data, A.name); }
//inline VolumeF normalize(const VolumeF& volume) { float sum = ::sum(volume); assert_(sum); return (1.f/sum) * volume; }

VolumeF normalize(VolumeF&& target) {
    target = scale(move(target), sq(target.size.x)/sum(target));
    assert_(abs(sum(target) -sq(target.size.x))<=0x1p-6, log2(abs(sum(target) -sq(target.size.x))));
    return move(target);
}

inline const VolumeF& cylinder(const VolumeF& target, const float value=1) {
    int3 size = target.size;
    const float2 center = float2(size.xy()-int2(1))/2.f;
    const float radiusSq = sq(center.x);
    for(uint z: range(size.z)) for(uint y: range(size.y)) for(uint x: range(size.x)) target(x,y,z) = sq(float2(x,y)-center)<=radiusSq ? value : 0;
    return target;
}
