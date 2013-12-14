#pragma once
#include "algebra.h"

/// Solves Ax=b using LU factorization
Vector solve(Matrix&& A, const Vector& b);
inline Vector solve(const Matrix& A, const Vector& b) { return solve(copy(A),b); }

/// Solves Ax[j]=e[j] using LU factorization
Matrix inverse(Matrix&& A);
inline Matrix inverse(const Matrix &A) { return inverse(copy(A)); }
