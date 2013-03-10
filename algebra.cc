#include "algebra.h"
#include "time.h"

Matrix operator*(const Matrix& a, const Matrix& b) {
    assert(a.n==b.m);
    Matrix r(a.m,b.n);
    for(uint i: range(r.m)) for(uint j: range(r.n)) for(uint k: range(a.n)) r(i,j) += a(i,k)*b(k,j);
    return r;
}

bool operator==(const Matrix& a,const Matrix& b) {
    assert(a.m==b.m && a.n==b.n);
    for(uint i: range(a.m)) for(uint j: range(a.n)) if(a(i,j)!=b(i,j)) return false;
    return true;
}

template<> string str(const Matrix& a) {
    string s("[ "_);
    for(uint i: range(a.m)) {
        if(a.n==1) s<<ftoa(a(i,0),0,0)<<' ';
        else {
            for(uint j: range(a.n)) {
                s<<ftoa(a(i,j),0,0)<<' ';
            }
            if(i<a.m-1) s<<"\n  "_;
        }
    }
    s << "]"_;
    return s;
}

// Swap row j with the row having the maximum value on column j, while maintaining a permutation matrix P
void pivot(Matrix &A, Permutation& P, uint j) {
    uint best=j; float maximum=abs<float>(A(best,j));
    for(uint i: range(j,A.m)) { // Find biggest on or below diagonal //TODO: sparse column iterator ?
        float value = abs<float>(A(i,j));
        if(value>maximum) best=i, maximum=value;
    }
    assert(maximum,A);
    if(best != j) { //swap rows i <-> j
        swap(A.rows[best],A.rows[j]);
        P.swap(best,j);
    }
}

PLU factorize(Matrix&& A) {
    assert(A.m==A.n);
    ScopeTimer timer;
    uint n = A.n;
    Permutation P(n);
    pivot(A, P, 0); // pivot first column
    float d = 1/A(0,0); for(Matrix::Element& e: A[0](1,n)) e.value *= d;
    // compute an L column, pivot to interchange rows, compute an U row.
    for(uint j: range(1,n-1)) {
        for(uint i: range(j,n)) { // L column
            float sum = 0;
            for(const Matrix::Element& e: A[i](0,j))  sum += e.value * A(e.column,j);
            A(i,j) -= sum;
        }
        pivot(A, P, j);
        float d = 1/A(j,j);
        for(uint k: range(j+1,n)) { //U row
            float sum = 0;
            for(const Matrix::Element& e: A[j](0,j))  sum += e.value * A(e.column,k);
            A(j,k) = (A(j,k)-sum)*d;
        }
    }
    // compute last L element
    float sum = 0;
    for(const Matrix::Element& e: A[n-1](0,n-1))  sum += e.value * A(e.column,n-1);
    A(n-1,n-1) -= sum;
    return {move(P), move(A)};
}

float determinant(const Permutation& P, const Matrix& LU) {
    float det = P.determinant();
    for(uint i: range(LU.n)) det *= LU(i,i);
    return det;
}

Vector solve(const Permutation& P, const Matrix &LU, const Vector& b) {
    assert(determinant(P,LU),"Coefficient matrix is singular"_);
    uint n=LU.n;
    Vector x(n);
    for(uint i: range(n)) x[i] = b[P[i]]; // Reorder b in x
    for(uint i: range(n)) { // Forward substitution from packed L
        for(const Matrix::Element& e: LU[i](0,i)) x[i] -= e.value * x[e.column];
        x[i] = x[i] / LU(i,i);
    }
    for(int i=n-2;i>=0;i--) { // Backward substition from packed U
        for(const Matrix::Element& e: LU[i](i+1,n)) x[i] -= e.value * x[e.column];
        //implicit ones on diagonal -> no division
    }
    return x;
}

Matrix inverse(const Permutation& P, const Matrix &LU) {
    uint n = LU.n;
    Matrix A_1(n,n);
    for(uint j=0;j<n;j++) {
        Vector e(n); for(uint i=0;i<n;i++) e[i] = 0; e[j] = 1;
        Vector x = solve(P,LU,move(e));
        for(uint i=0;i<n;i++) A_1(i,j) = move(x[i]);
    }
    return A_1;
}
