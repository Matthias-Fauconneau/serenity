#pragma once
#include "opencl.h"

float sum(const CLVolume& A, const int3 origin=0, const int3 size=0);
float SSQ(const CLVolume& A, const int3 origin=0, const int3 size=0);
float SSE(const CLVolume& A, const CLVolume& B, const int3 origin=0, const int3 size=0);
float dotProduct(const CLVolume& A, const CLVolume& B, const int3 origin=0, const int3 size=0);

CL(operators, add) inline uint64 add(const CLVolume& y, const float alpha, const CLVolume& a, const float beta, const CLVolume& b) { return emulateWriteTo3DImage(CL::add, y, noneNearestSampler, alpha, a, beta, b); } // y = α a + β b
CL(operators, delta) inline uint64 delta(const ImageArray& y, const ImageArray& a, const ImageArray& b, const ImageArray& c) { return emulateWriteTo3DImage(CL::delta, y, noneNearestSampler, a, b, c); } // y = ( a - b ) / c
CL(operators, update) inline uint64 update(const CLVolume& y, const CLVolume& a, const float alpha, const CLVolume& b) { return emulateWriteTo3DImage(CL::update, y, noneNearestSampler, a, alpha, b); } // y = max(0, a + α b)

#include "volume.h"
inline void cylinderCheck(CLVolume& A) {
    int3 size = A.size;
    const float2 center = float2(size.xy()-int2(1))/2.f;
    const float radiusSq = sq(center.x);
    VolumeF volume(size); A.read(volume); for(uint z: range(size.z)) for(uint y: range(size.y)) for(uint x: range(size.x)) if(sq(float2(x,y)-center)>radiusSq) assert_(volume(x,y,z) == 0, x,y,z);
}
