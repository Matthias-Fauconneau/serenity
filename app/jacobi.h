#pragma once
#include "memory.h"

typedef buffer<float> Vector;
struct Matrix : buffer<float> {
    uint M/*rows*/, N/*columns*/;
    Matrix(uint M, uint N) : buffer<float>(M*N), M(M), N(N) {}
    const float& operator()(uint i, uint j) const { return at(j*M+i); }
    float& operator()(uint i, uint j) { return at(j*M+i); }
    ref<float> operator[](uint j) const { return slice(j*M, M); }
};
struct USV { Matrix U; Vector S; Matrix V; };

USV SVD(const Matrix& A);
