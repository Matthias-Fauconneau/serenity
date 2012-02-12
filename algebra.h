#pragma once
#include "core.h"

/// Matrix for (TODO:sparse) linear algebra
struct Matrix {
    no_copy(Matrix)
    Matrix(Matrix&& o) : data(o.data), m(o.m), n(o.n) { o.data=0; }
    Matrix& operator=(Matrix&& o) {assert(&o!=this); this->~Matrix(); data=o.data; m=o.m; n=o.n; o.data=0; return *this; }
    /// Allocate a m-row, n-column matrix
    Matrix(int m, int n):data(new T[m*n]),m(m),n(n){debug(set((int*)data,m*n,*(int*)&NaN);)}
    ~Matrix() { if(data) delete data; }

    float operator()(int i, int j) const { assert(data && i>=0 && i<m && j>=0 && j<n); return data[j*m+i]; }
    float& operator()(int i, int j) { assert(data && i>=0 && i<m && j>=0 && j<n); return data[j*m+i]; }

    float* data; /// elements stored in column-major order
    int m=-1,n=-1; /// row and column count
};

/// Vector for linear algebra (i.e n-row, single column matrix)
struct Vector : Matrix {
    Vector(Matrix&& o):Matrix(move(o)){ assert(n==1); }
    Vector(int n):Matrix(n,1){}
    float operator[](int i) const { assert(data && i>=0 && i<m); return data[i]; }
    float& operator[](int i) { assert(data && i>=0 && i<m); return data[i]; }
};

/// Logs to standard text output
template<> void log_(const Matrix& a) {
    string s="["_;
    for(int i=0;i<a.m;i++) {
        if(a.n==1) s = s+"\t"_+toString(a(i,0));
        else {
            for(int j=0;j<a.n;j++) {
                s = s+"\t"_+toString(a(i,j));
            }
            if(i<a.m-1) s=s+"\n"_;
        }
    }
    s=s+" ]"_;
    log_(s);
}
template<> void log_(const Vector& a) { log_<Matrix>(a); }

/// Returns true if both matrices are identical
bool operator==(const Matrix& a,const Matrix& b) {
    assert(a.m==b.m && a.n==b.n);
    for(int i=0;i<a.m;i++) for(int j=0;j<a.n;j++) if(a(i,j)!=b(i,j)) return false;
    return true;
}

/// Transposition (reflect over main diagonal)
//TODO: implicit transposition
Matrix transpose(const Matrix& a) {
    Matrix r(a.n,a.m);
    for(int i=0;i<r.m;i++) for(int j=0;j<r.n;j++) r(i,j)=a(j,i);
    return r;
}

/// Matrix multiplication (composition of linear transformations)
Matrix operator*(const Matrix& a,const Matrix& b) {
    assert(a.n==b.m);
    Matrix r(a.m,b.n);
    for(int i=0;i<r.m;i++) for(int j=0;j<r.n;j++) { r(i,j)=0; for(int k=0;k<a.n;k++) r(i,j) += a(i,k)*b(k,j); }
    return r;
}

/// Factorizes a symmetric, positive-definite matrix as the product of a lower triangular matrix and its transpose
void cholesky(Matrix& a) {
    assert(transpose(a)==a);
    for(int j=0;j<a.n;j++) {
        debug(for(int i=0;i<j;i++) a(i,j)=0;)
        a(j,j)=sqrt(a(j,j));
        for(int i=j+1;i<a.m;i++) a(i,j) /= a(j,j);
        for(int k=j+1;k<a.m;k++)
            for(int i=k;i<a.m;i++) a(i,k) -= a(i,j)*a(k,j);
    }
}

/// Solves Lx=b using forward substitution
Vector forward(const Matrix& l, const Vector& b) {
    assert(l.m==l.n && l.n==b.m && b.n==1);
    Vector x(l.n);
    for(int i=0;i<l.n;i++) {
        x[i] = b[i];
        for(int j=0;j<i;j++) x[i] -= l(i,j) * x[j];
        x[i] /= l(i, i);
    }
    return x;
}

/// Solves Ux=b using backward substitution
Vector backward(const Matrix& u, const Vector& b) {
    assert(u.m==u.n && u.n==b.m && b.n==1);
    Vector x(u.n);
    for(int i=u.n-1;i>=0;i--) {
        x[i] = b[i];
        for(int j=i+1;j<u.n;j++) x[i] -= u(i,j) * x[j];
        x[i] /= u(i, i);
    }
    return x;
}

/// Solves the linear least squares minimization |Ax-b|²
//TODO: sparse cholesky
Vector solve(const Matrix& A, const Vector& b) {
    //solve the normal equations (At·A)x=(At)b
    Matrix AtA = transpose(A)*A;
    Vector Atb = transpose(A)*b;
    // Solves AtAx=Atb using Cholesky decomposition (A=LL*), forward substitution (Lx = b) and backward substitution (Ux = y)
    // \note AtA is a symmetric, positive-definite matrix
    cholesky(AtA);
    return backward(transpose(AtA),forward(AtA,Atb));
}
