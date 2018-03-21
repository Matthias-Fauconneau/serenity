#pragma once
#include "memory.h"

typedef buffer<float> Vector;
struct Matrix : buffer<float> {
    uint M, N;
    Matrix(uint M, uint N) : buffer<float>(M*N), M(M), N(N) {}
    const float& operator()(uint i, uint j) const { return at(i*M+j); }
    float& operator()(uint i, uint j) { return at(i*M+j); }
};
struct USV { Matrix U; Vector S; Matrix V; };

USV SVD(const Matrix& A);
