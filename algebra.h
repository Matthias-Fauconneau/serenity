#pragma once
/// Symbolic linear algebra (matrix operations, linear solver)
#include "expression.h"

/// Matrix for symbolic linear algebra
struct Matrix {
    no_copy(Matrix)
    Matrix(Matrix&& o) : data(o.data), m(o.m), n(o.n) { o.data=0; }
    Matrix& operator=(Matrix&& o) {assert(&o!=this); this->~Matrix(); data=o.data; m=o.m; n=o.n; o.data=0; return *this; }
    /// Allocate a m-row, n-column matrix initialized to invalid expressions
    Matrix(int m, int n):data(new Expression[m*n]),m(m),n(n) {}
    Matrix(const std::initializer_list< std::initializer_list<Expression> >& list) : Matrix(list.begin()[0].size(),list.size()) {
        int i=0; for(auto& row: list) { int j=0; for(auto& e: row) { at(i,j)=Expression(::copy(e)); j++; } assert(j==n); i++; }
    }
    ~Matrix() { if(data) delete[] data; }

    const Expression& at(int i, int j) const { assert(data && i>=0 && i<m && j>=0 && j<n); return data[j*m+i]; }
    Expression& at(int i, int j) { assert(data && i>=0 && i<m && j>=0 && j<n); return data[j*m+i]; }
    const Expression& operator()(int i, int j) const { return at(i,j); }
    Expression& operator()(int i, int j) { return at(i,j); }

    void zero() { for(int i=0;i<m;i++) for(int j=0;j<n;j++) at(i,j)=0; }

    Expression* data; /// elements stored in column-major order
    int m=-1,n=-1; /// row and column count
};
template<> inline Matrix copy(const Matrix& a) { Matrix o(a.m,a.n); copy(o.data,a.data,a.m*a.n); return o; }

/// Returns true if both matrices are identical
bool operator==(const Matrix& a,const Matrix& b);
inline bool operator!=(const Matrix& a,const Matrix& b) { return !(a==b); }

/// Matrix multiplication (composition of linear transformations)
Matrix operator*(const Matrix& a,const Matrix& b);

/// Logs to standard text output
template<> string str(const Matrix& a);

/// Permutation matrix
struct Permutation {
    move_only(Permutation)
    int even=1; //1 if even count of swaps, -1 if odd count of swaps (used for determinant)
    array<int> order;
    Permutation(int n):order(n) { order.size=n; for(int i=0;i<n;i++) order[i] = i; } // identity ordering
    void swap(int i, int j) { ::swap(order[i],order[j]); even=-even; }
    int determinant() const { return even; }
    int operator[](int i) const { return order[i]; } //b[P[i]] = (P*b)[i]
};

Matrix operator *(const Permutation& P, Matrix&& A);

// Swap row j with the row having the largest value on column j, while maintaining a permutation matrix P
void pivot(Matrix &A, Permutation& P, int j);

/// Factorizes any matrix as the product of a lower triangular matrix and an upper triangular matrix
/// \return permutations (P) and packed LU (U's diagonal is 1).
struct PLU { Permutation P; Matrix LU; };
PLU factorize(Matrix&& A);

/// Returns L and U from a packed LU matrix.
struct LU { Matrix L,U; };
LU unpack(Matrix&& LU);

/// Compute determinant of a packed PLU matrix (product along diagonal)
Expression determinant(const Permutation& P, const Matrix& LU);

/// Vector for symbolic linear algebra (i.e n-row, single column matrix)
struct Vector : Matrix {
    Vector(Matrix&& o):Matrix(move(o)){ assert(n==1); }
    Vector(int n):Matrix(n,1){}
    Vector(const std::initializer_list<Expression>& list) : Vector(list.size()) {
        int i=0; for(auto& e: list) { at(i,0)=Expression(::copy(e)); i++; }
    }
    const Expression& operator[](int i) const { assert(data && i>=0 && i<m); return data[i]; }
    Expression& operator[](int i) { assert(data && i>=0 && i<m); return data[i]; }
};
template<> inline Vector copy(const Vector& a) { return copy<Matrix>(a); }
template<> inline string str(const Vector& a) { return str<Matrix>(a); }

/// Solves PLUx=b
Vector solve(const Permutation& P, const Matrix &LU, Vector&& b);

/// Solves Ax[j]=e[j] using LU factorization
Matrix inverse(const Matrix &A);

/// Solves Ax=b using LU factorization
Vector solve(const Matrix& A, const Vector& b);
