#pragma once
#include "array.h"

typedef double real;
typedef buffer<real> Vector;
inline Vector operator*(real a, const Vector& x) {
    Vector y(x.size);
    for(uint i: range(y.size)) y[i] = a * x[i];
    return y;
}
inline Vector operator+(const Vector& a, const Vector& b) {
    assert(a.size == b.size);
    Vector v(a.size);
    for(uint i: range(v.size)) v[i] = a[i] + b[i];
    return v;
}
inline Vector operator*(const Vector& a, const Vector& b) {
    assert(a.size == b.size);
    Vector v(a.size);
    for(uint i: range(v.size)) v[i] = a[i] * b[i];
    return v;
}

struct Matrix {
    uint m,n;
    buffer<real> elements;
    Matrix(uint m, uint n) : m(m), n(n), elements(m*n) { elements.clear(__builtin_nan("")); }
    Matrix(uint n) : Matrix(n,n) {}
    const real& operator ()(uint i, uint j) const { return elements[j*m+i]; } // Column major storage
    real& operator ()(uint i, uint j) { return elements[j*m+i]; }
};
inline Matrix copy(const Matrix& o) { Matrix t(o.m,o.n); t.elements=copy(o.elements); return move(t); }
String str(const Matrix& A);
inline Vector operator*(const Matrix& A, const Vector& x) {
    assert(A.n == x.size);
    Vector y (A.n);
    for(uint i: range(A.m)) { y[i]=0; for(uint k: range(A.n)) y[i] += A(i,k)*x[k]; }
    return y;
}
inline Matrix operator*(const Matrix& A, const Matrix& B) {
    assert(A.n == B.m);
    Matrix M(A.m, B.n);
    for(uint i: range(A.m)) for(uint j: range(B.n)) { M(i,j)=0; for(uint k: range(A.n)) M(i,j) += A(i,k)*B(k,j); }
    return M;
}
