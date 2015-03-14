#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "string.h"
#define NaN __builtin_nan("")

/// Dense matrix
struct Matrix {
	size_t m=0,n=0; /// row and column count
	buffer<float> elements;

    default_move(Matrix);
	Matrix(uint m, uint n) : m(m), n(n), elements(m*n) { elements.clear(0); }

	float operator()(uint i, uint j) const { return elements[i*n+j]; }
	float& operator()(uint i, uint j) { return elements[i*n+j]; }
};

Matrix transpose(const Matrix& A);
Matrix operator*(const Matrix& A, const Matrix& B);
bool operator==(const Matrix& A, const Matrix& B);
String str(const Matrix& A);

/// Dense vector
typedef buffer<float> Vector;
Vector operator*(const Matrix& A, const Vector& b);
Vector operator-(const Vector& a, const Vector& b);

/// Permutation matrix
struct Permutation {  
    int even=1; //1 if even count of swaps, -1 if odd count of swaps (used for determinant)
    array<int> order;

    Permutation(){}
    Permutation(int n) : order(n,n) { for(uint i: range(n)) order[i] = i; } // identity ordering
    void swap(int i, int j) { ::swap(order[i],order[j]); even=-even; }
    int determinant() const { return even; }
    int operator[](int i) const { return order[i]; } //b[P[i]] = (P*b)[i]
};

/// Factorizes an invertible matrix as the product of a lower triangular matrix and an upper triangular matrix
/// \return permutations (P) and lower L and upper U.
struct PLU { Permutation P; Matrix L; Matrix U; };
PLU factorize(const Matrix& A);

/// Compute determinant of PLU
float determinant(const Permutation& P, const Matrix& L, const Matrix& U);

/// Solves PLUx=b
Vector solve(const Permutation& P, const Matrix& L, const Matrix& U, const Vector& b);
/// Solves PLUx=b
inline Vector solve(const PLU& PLU, const Vector& b) { return solve(PLU.P, PLU.L, PLU.U, b); }
/// Solves Ax=b using LU factorization
inline Vector solve(Matrix&& A, const Vector& b) { PLU PLU = factorize(move(A)); return solve(PLU.P,PLU.L,PLU.U,b); }
