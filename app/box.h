#pragma once
#include "core.h"

template<int R> static inline void box(float *const target, float *const buffer, float *const image, uint X, uint Y) {
    for(const uint y: range(Y)) {
        const float* sourceLine = image+y*X;
        float* const targetLine = buffer+y;
        float sum = R * sourceLine[0];
        for(const uint x: range(R)) sum += sourceLine[x];
        for(const uint x: range(R)) {
            sum += sourceLine[x+R];
            targetLine[x*Y] = sum; // Transposes
            sum -= sourceLine[0];
        }
        for(const uint x: range(R, X-R)) {
            sum += sourceLine[x+R];
            targetLine[x*Y] = sum; // Transposes
            sum -= sourceLine[x-R];
        }
        for(const uint x: range(X-R, X)) {
            sum += sourceLine[X-1];
            targetLine[x*Y] = sum; // Transposes
            sum -= sourceLine[x-R];
        }
    }
    constexpr float w = 1 / float(sq(1+2*R));
    for(const uint y: range(X)) {
        const float* sourceLine = buffer+y*Y;
        float* const targetLine = target+y;
        float sum = R * sourceLine[0];
        for(const uint x: range(R)) sum += sourceLine[x];
        for(const uint x: range(R)) {
            sum += sourceLine[x+R];
            targetLine[x*X] = w * sum; // Transposes back
            sum -= sourceLine[0];
        }
        for(const uint x: range(R, Y-R)) {
            sum += sourceLine[x+R];
            targetLine[x*X] = w * sum; // Transposes back
            sum -= sourceLine[x-R];
        }
        for(const uint x: range(Y-R, Y)) {
            sum += sourceLine[Y-1];
            targetLine[x*X] = w * sum; // Transposes back
            sum -= sourceLine[x-R];
        }
    }
}
