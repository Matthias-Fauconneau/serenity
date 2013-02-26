#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "memory.h"
#include "string.h"
#define NaN __builtin_nan("")

/// Matrix for numeric linear algebra
struct Matrix {
    default_move(Matrix);
    /// Allocate a m-row, n-column matrix initialized to Nan
    Matrix(uint m, uint n):data(m*n,NaN),m(m),n(n) {}
    template<size_t N> Matrix(const float (&a)[N]):Matrix(sqrt(N),sqrt(N)) {
        for(uint i: range(m)) for(uint j: range(n)) at(i,j)=a[i*m+j];
    }

    const float& at(uint i, uint j) const { assert(data && i<m && j<n); return data[j*m+i]; }
    float& at(uint i, uint j) { assert(data && i<m && j<n); return data[j*m+i]; }
    const float& operator()(uint i, uint j) const { return at(i,j); }
    float& operator()(uint i, uint j) { return at(i,j); }

    void clear() { for(uint i=0;i<m;i++) for(uint j=0;j<n;j++) at(i,j)=0; }

    buffer<float> data; /// elements stored in column-major order
    uint m=0,n=0; /// row and column count
};
template<> inline Matrix copy(const Matrix& o) { Matrix t(o.m,o.n); t.data=copy(o.data); return move(t); }

/// Returns true if both matrices are identical
bool operator==(const Matrix& a,const Matrix& b);
inline bool operator!=(const Matrix& a,const Matrix& b) { return !(a==b); }

/// Matrix multiplication (composition of linear transformations)
Matrix operator*(const Matrix& a,const Matrix& b);

/// Converts matrix to text
template<> string str(const Matrix& a);

/// Permutation matrix
struct Permutation {  
    int even=1; //1 if even count of swaps, -1 if odd count of swaps (used for determinant)
    array<int> order;

    //default_move(Permutation);
    Permutation(int n) : order(n) { order.setSize(n); for(int i=0;i<n;i++) order[i] = i; } // identity ordering
    void swap(int i, int j) { ::swap(order[i],order[j]); even=-even; }
    int determinant() const { return even; }
    int operator[](int i) const { return order[i]; } //b[P[i]] = (P*b)[i]
};

Matrix operator *(const Permutation& P, Matrix&& A);

/// Converts permutation matrix to text
template<> string str(const Permutation& P);

// Swap row j with the row having the largest value on column j, while maintaining a permutation matrix P
void pivot(Matrix &A, Permutation& P, uint j);

/// Factorizes any matrix as the product of a lower triangular matrix and an upper triangular matrix
/// \return permutations (P) and packed LU (U's diagonal is 1).
struct PLU { Permutation P; Matrix LU; };
PLU factorize(Matrix&& A);

/// Returns L and U from a packed LU matrix.
struct LU { Matrix L,U; };
LU unpack(Matrix&& LU);

/// Convenience macro to emulate multiple return arguments
#define multi(A, B, F) auto A##B_ F auto A = move(A##B_.A); auto B=move(A##B_.B);

/// Compute determinant of a packed PLU matrix (product along diagonal)
float determinant(const Permutation& P, const Matrix& LU);

/// Vector for symbolic linear algebra (i.e n-row, single column matrix)
struct Vector : Matrix {
    //Vector(Matrix&& o):Matrix(move(o)){ assert(n==1); }
    Vector(int n):Matrix(n,1){}
    Vector(const ref<float>& list) : Vector(list.size) {
        int i=0; for(auto& e: list) { at(i,0)=e; i++; }
    }
    const float& operator[](uint i) const { assert(data && i<m); return data[i]; }
    float& operator[](uint i) { assert(data && i<m); return data[i]; }
    const float& operator()(uint i) const { assert(data && i<m); return data[i]; }
    float& operator()(uint i) { assert(data && i<m); return data[i]; }
};
//template<> inline Vector copy(const Vector& a) { return copy<Matrix>(a); }
template<> inline string str(const Vector& a) { return str<Matrix>(a); }

/// Solves PLUx=b
Vector solve(const Permutation& P, const Matrix &LU, const Vector& b);

/// Solves Ax[j]=e[j] using LU factorization
Matrix inverse(const Matrix &A);

/// Solves Ax=b using LU factorization
Vector solve(const Matrix& A, const Vector& b);
