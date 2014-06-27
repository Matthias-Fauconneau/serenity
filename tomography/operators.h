#pragma once
#include "opencl.h"

float SSE(const CLVolume& A, const CLVolume& B, const int3 origin=0, int3 size=0);
float dot(const CLVolume& A, const CLVolume& B, const int3 origin=0, int3 size=0);

CL(operators, divdiff) //FIXME: dummy as cl() is not parsed by build
//inline uint64 mul(const ImageArray& y, const ImageArray& a, const ImageArray& b) { cl(operators, mul); return emulateWriteTo3DImage(mul, y, noneNearestSampler, a, b); } // y = a * b [MLEM]
//inline uint64 div(const ImageArray& y, const ImageArray& a, const ImageArray& b) { cl(operators, div) return emulateWriteTo3DImage(div, y, noneNearestSampler, a, b); } // y = a / b [MLEM]
inline uint64 divdiff(const ImageArray& y, const ImageArray& a, const ImageArray& b, const ImageArray& c) { cl(operators, divdiff) return emulateWriteTo3DImage(divdiff, y, noneNearestSampler, a, b, c); } // y = ( a - b ) / c [SART]
inline uint64 divmul(const ImageArray& y, const ImageArray& a, const ImageArray& b, const ImageArray& c) {cl(operators, divmul) return emulateWriteTo3DImage(divmul, y, noneNearestSampler, a, b, c); } // y = a / (b * c) [MLEM]
inline uint64 maxadd(const CLVolume& y, const CLVolume& a, const float alpha, const CLVolume& b) { cl(operators, maxadd) return emulateWriteTo3DImage(maxadd, y, noneNearestSampler, a, alpha, b); } // y = max(0, a + α b) [SART, CG]
//inline uint64 add(const CLVolume& y, const float alpha, const CLVolume& a, const float beta, const CLVolume& b) { cl(operators, add) return emulateWriteTo3DImage(add, y, noneNearestSampler, alpha, a, beta, b); } // y = α a + β b [CG]
inline uint64 mulexp(const CLVolume& y, const CLVolume& a, const CLVolume& b) { cl(operators, mulexp) return emulateWriteTo3DImage(mulexp, y, noneNearestSampler, a, b); } // y = a · exp(-b) [MLTR]
inline uint64 diffexp(const CLVolume& y, const CLVolume& a, const CLVolume& b) { cl(operators, diffexp) return emulateWriteTo3DImage(diffexp, y, noneNearestSampler, a, b); } // y = exp(-a) - exp(-b) [MLTR]
inline uint64 adddiv(const CLVolume& y, const CLVolume& a, const CLVolume& b, const CLVolume& c) { cl(operators, adddiv) return emulateWriteTo3DImage(adddiv, y, noneNearestSampler, a, b, c); } // y = max(0, a + c ? b / c : 0) [MLTR]
inline uint64 muldiv(const CLVolume& y, const CLVolume& a, const CLVolume& b, const CLVolume& c) { cl(operators, muldiv) return emulateWriteTo3DImage(muldiv, y, noneNearestSampler, a, b, c); } // y = max(0, a + c ? a * b / c : 0) [MLTR, MLEM]
inline uint64 negln(const ImageArray& y, const ImageArray& a) { cl(operators, negln) return emulateWriteTo3DImage(negln, y, noneNearestSampler, a); } // y = ln(a) [SART, CG]
inline ImageArray negln(const ImageArray& a) { ImageArray y(a.size, 0, "ln "_+a.name); negln(y, a); return y; }
