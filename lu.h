#pragma once
#include "algebra.h"

/// Solves Ax[j]=e[j] using LU factorization
Vector solve(Matrix&& A, const Vector& b);
inline Vector solve(const Matrix& A, const Vector& b) { return solve(copy(A),b); }
