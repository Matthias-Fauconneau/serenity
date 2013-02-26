#include "algebra.h"

bool operator==(const Matrix& a,const Matrix& b) {
    assert(a.m==b.m && a.n==b.n);
    for(uint i=0;i<a.m;i++) for(uint j=0;j<a.n;j++) if(a(i,j)!=b(i,j)) return false;
    return true;
}

Matrix operator*(const Matrix& a,const Matrix& b) {
    assert(a.n==b.m);
    Matrix r(a.m,b.n);
    for(uint i=0;i<r.m;i++) for(uint j=0;j<r.n;j++) { r(i,j)=0; for(uint k=0;k<a.n;k++) r(i,j) += a(i,k)*b(k,j); }
    return r;
}

template<> string str(const Matrix& a) {
    string s("["_);
    for(uint i=0;i<a.m;i++) {
        if(a.n==1) s<< "\t"_+str(a(i,0));
        else {
            for(uint j=0;j<a.n;j++) {
                s<< "\t"_+str(a(i,j));
            }
            if(i<a.m-1) s<<"\n"_;
        }
    }
    s << " ]"_;
    return s;
}

Matrix operator *(const Permutation& P, Matrix&& A) {
    assert(P.order.size()==A.m && P.order.size()==A.n);
    Matrix PA(A.m,A.n);
    for(uint i=0;i<A.m;i++) for(uint j=0;j<A.n;j++) PA(P[i],j)=move(A(i,j));
    return PA;
}

Matrix identity(uint size) { Matrix I(size,size); I.clear(); for(uint i: range(size)) I(i,i)=1; return I; }

template<> string str(const Permutation& P) { return str(P*identity(P.order.size())); }

// Swap row j with the row having the largest value on column j, while maintaining a permutation matrix P
void pivot(Matrix &A, Permutation& P, uint j) {
    uint best=j;
    for(uint i = j;i<A.m;i++) { // Find biggest on or below diagonal
        if(abs(A(i,j))>abs(A(best,j))) best=i;
    }
    assert(A(best,j),A);
    if(best != j) { //swap rows i <-> j
        for(uint k=0;k<A.n;k++) swap(A(best,k),A(j,k));
        P.swap(best,j);
    }
}

PLU factorize(Matrix&& A) {
    assert(A.m==A.n);
    uint n = A.n;
    Permutation P(n);
    // pivot first column
    pivot(A, P, 0);
    float d = 1/A(0,0); for(uint i=1;i<n;i++) A(0,i) *= d;
    // compute an L column, pivot to interchange rows, compute an U row.
    for (uint j=1;j<n-1;j++) {
        for(uint i=j;i<n;i++) { // L column
            float sum = 0;
            for(uint k=0;k<j;k++) sum += A(i,k)*A(k,j);
            A(i,j) -= sum;
        }
        pivot(A, P, j); //pivot
        float d = 1/A(j,j);
        for(uint k=j+1;k<n;k++) { //U row
            float sum = 0;
            for(uint i=0; i<j; i++) sum += A(j,i)*A(i,k);
            A(j,k) = (A(j,k)-sum)*d;
        }
    }
    // compute last L element
    float sum = 0;
    for(uint k=0;k<n-1;k++) sum += A(n-1,k)*A(k,n-1);
    A(n-1,n-1) -= sum;
    return __( move(P), move(A) );
}

LU unpack(Matrix&& LU) {
    assert(LU.m==LU.n);
    Matrix L = move(LU);
    Matrix U(L.m,L.n);
    for(uint i=0;i<L.m;i++) {
        for(uint j=0;j<i;j++) U(i,j) = 0;
        U(i,i) = 1;
        for(uint j=i+1;j<L.n;j++) U(i,j)=move(L(i,j)), L(i,j) = 0;
    }
    return __( move(L), move(U) );
}

float determinant(const Permutation& P, const Matrix& LU) {
    float det = P.determinant();
    for(uint i=0;i<LU.n;i++) det *= LU(i,i);
    return det;
}

Vector solve(const Permutation& P, const Matrix &LU, const Vector& b) {
    assert(determinant(P,LU),"Coefficient matrix is singular"_);
    uint n=LU.n;
    Vector x(n);
    for(uint i=0;i<n;i++) x[i] = b[P[i]]; // Reorder b in x
    for(uint i=0;i<n;i++) { // Forward substitution from packed L
        for(uint j=0;j<i;j++) x[i] -= LU(i,j)*x[j];
        x[i] = x[i]*(1/LU(i,i));
    }
    for(int i=n-2;i>=0;i--) { // Backward substition from packed U
        for (uint j=i+1; j<n; j++) x[i] -= LU(i,j) * x[j];
        //implicit ones on diagonal -> no division
    }
    return x;
}

Matrix inverse(const Matrix &A) {
    multi(P,LU, = factorize(copy(A)); ) //compute P,LU
    multi(L,U, = unpack(copy(LU)); ) //unpack LU -> L,U
    //if(A!=P*(L*U)) log("|A|",determinant(P,LU)), log("A",A), log(P*(L*U)), error("A==P*(L*U)");
    uint n = A.n;
    Matrix A_1(n,n);
    for(uint j=0;j<n;j++) {
        Vector e(n); for(uint i=0;i<n;i++) e[i] = 0; e[j] = 1;
        Vector x = solve(P,LU,move(e));
        for(uint i=0;i<n;i++) A_1(i,j) = move(x[i]);
    }
    Matrix I(n,n); for(uint i=0;i<n;i++) { for(uint j=0;j<n;j++) I(i,j)=0; I(i,i)=1; }
    assert(A_1*A==I,A_1*A);
    return A_1;
}

Vector solve(const Matrix& A, const Vector& b) {
    multi(P,LU, = factorize(copy(A)); ) //compute P,LU
    multi(L,U, = unpack(copy(LU)); ) //unpack LU -> L,U
    //if(A!=P*(L*U)) log("|A|",determinant(P,LU)), log("A",A), log(P*(L*U)), error("A==P*(L*U)");
    Vector x = solve(P,LU,b);
    return x;
}

