#pragma once
#include "opencl.h"

float sum(const CLVolume& A, const int3 origin=0, int3 size=0);
inline float mean(const CLVolume& A) { return sum(A) / (A.size.x*A.size.y*A.size.z); }
float SSQ(const CLVolume& A, const int3 origin=0, int3 size=0);
float SSE(const CLVolume& A, const CLVolume& B, const int3 origin=0, int3 size=0);
float dotProduct(const CLVolume& A, const CLVolume& B, const int3 origin=0, int3 size=0);

CL(operators, mul) inline uint64 mul(const ImageArray& y, const ImageArray& a, const ImageArray& b) { return emulateWriteTo3DImage(CL::mul, y, noneNearestSampler, a, b); } // y = a * b [MLEM]
CL(operators, div) inline uint64 div(const ImageArray& y, const ImageArray& a, const ImageArray& b) { return emulateWriteTo3DImage(CL::div, y, noneNearestSampler, a, b); } // y = a / b [MLEM]
CL(operators, divdiff) inline uint64 divdiff(const ImageArray& y, const ImageArray& a, const ImageArray& b, const ImageArray& c) { return emulateWriteTo3DImage(CL::divdiff, y, noneNearestSampler, a, b, c); } // y = ( a - b ) / c [SART]
CL(operators, divdiv) inline uint64 divdiv(const ImageArray& y, const ImageArray& a, const ImageArray& b, const ImageArray& c) { return emulateWriteTo3DImage(CL::divdiff, y, noneNearestSampler, a, b, c); } // y = ( a / b ) / c [MLEM]
CL(operators, maxadd) inline uint64 maxadd(const CLVolume& y, const CLVolume& a, const float alpha, const CLVolume& b) { return emulateWriteTo3DImage(CL::maxadd, y, noneNearestSampler, a, alpha, b); } // y = max(0, a + α b) [SART, CG]
CL(operators, add) inline uint64 add(const CLVolume& y, const float alpha, const CLVolume& a, const float beta, const CLVolume& b) { return emulateWriteTo3DImage(CL::add, y, noneNearestSampler, alpha, a, beta, b); } // y = α a + β b [CG]
CL(operators, mulexp) inline uint64 mulexp(const CLVolume& y, const CLVolume& a, const CLVolume& b) { return emulateWriteTo3DImage(CL::mulexp, y, noneNearestSampler, a, b); } // y = a · exp(-b) [MLTR]
CL(operators, diffexp) inline uint64 diffexp(const CLVolume& y, const CLVolume& a, const CLVolume& b) { return emulateWriteTo3DImage(CL::diffexp, y, noneNearestSampler, a, b); } // y = exp(-a) - exp(-b) [MLTR]
CL(operators, adddiv) inline uint64 adddiv(const CLVolume& y, const CLVolume& a, const CLVolume& b, const CLVolume& c) { return emulateWriteTo3DImage(CL::adddiv, y, noneNearestSampler, a, b, c); } // y = max(0, a + c ? b / c : 0) [MLTR]
CL(operators, muldiv) inline uint64 muldiv(const CLVolume& y, const CLVolume& a, const CLVolume& b, const CLVolume& c) { return emulateWriteTo3DImage(CL::muldiv, y, noneNearestSampler, a, b, c); } // y = max(0, a + c ? a * b / c : 0) [MLTR, MLEM]
CL(operators, ln) inline uint64 ln(const ImageArray& y, const ImageArray& a) { return emulateWriteTo3DImage(CL::ln, y, noneNearestSampler, a); } // y = ln(a) [SARTL]
CL(operators, _exp) inline uint64 exp(const ImageArray& y, const ImageArray& a) { return emulateWriteTo3DImage(CL::_exp, y, noneNearestSampler, a); } // y = exp(a) [SARTL]

#include "volume.h"
inline void cylinderCheck(CLVolume& A) {
    int3 size = A.size;
    const float2 center = float2(size.xy()-int2(1))/2.f;
    const float radiusSq = sq(center.x);
    VolumeF volume(size); A.read(volume); for(uint z: range(size.z)) for(uint y: range(size.y)) for(uint x: range(size.x)) if(sq(float2(x,y)-center)>radiusSq) assert_(volume(x,y,z) == 0, x,y,z);
}
