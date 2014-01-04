#pragma once
#include "algebra.h"

/// Permutation matrix
struct Permutation {
    int even=1; //1 if even count of swaps, -1 if odd count of swaps (used for determinant)
    buffer<int> order;

    Permutation(int n) : order(n) { order.size=n; for(uint i: range(n)) order[i] = i; } // identity ordering
    void swap(int i, int j) { ::swap(order[i],order[j]); even=-even; }
    int determinant() const { return even; }
    int operator[](int i) const { return order[i]; } //b[P[i]] = (P*b)[i]
};

struct PLU { Permutation P; Matrix LU; };
/// Factorizes any matrix as the product of a lower triangular matrix and an upper triangular matrix
/// \return permutations (P) and packed LU (U's diagonal is 1).
PLU factorize(Matrix&& LU);

/// Solves PLUx=b using LU factorization
Vector solve(const Permutation& P, const Matrix& LU, const Vector& b);
inline Vector solve(const PLU& PLU, const Vector& b) { return solve(PLU.P, PLU.LU, b); }

/// Solves Ax=b using LU factorization
Vector solve(Matrix&& A, const Vector& b);
inline Vector solve(const Matrix& A, const Vector& b) { return solve(copy(A),b); }

/// Solves Ax[j]=e[j] using LU factorization
Matrix inverse(Matrix&& A);
inline Matrix inverse(const Matrix &A) { return inverse(copy(A)); }
