#include "algebra.h"

bool operator==(const Matrix& a,const Matrix& b) {
    assert(a.m==b.m && a.n==b.n);
    for(uint i: range(a.m)) for(uint j: range(a.n)) if(a(i,j)!=b(i,j)) return false;
    return true;
}

Matrix operator*(const Matrix& a,const Matrix& b) {
    assert(a.n==b.m);
    Matrix r(a.m,b.n);
    for(uint i: range(r.m)) for(uint j: range(r.n)) { r(i,j)=0; for(uint k: range(a.n)) r(i,j) += a(i,k)*b(k,j); }
    return r;
}

template<> string str(const Matrix& a) {
    string s("[ "_);
    for(uint i: range(a.m)) {
        if(a.n==1) s<<ftoa(a(i,0),0,3)<<' ';
        else {
            for(uint j: range(a.n)) {
                s<<ftoa(a(i,j),0,3)<<' ';
            }
            if(i<a.m-1) s<<"\n  "_;
        }
    }
    s << "]"_;
    return s;
}

/*Matrix operator *(const Permutation& P, Matrix&& A) {
    assert(P.order.size==A.m && P.order.size==A.n);
    Matrix PA(A.m,A.n);
    for(uint i: range(A.m)) for(uint j: range(A.n)) PA(P[i],j)=A(i,j);
    return PA;
}*/

Matrix identity(uint size) { Matrix I(size,size); for(uint i: range(size)) I(i,i)=1; return I; }

//template<> string str(const Permutation& P) { return str(P*identity(P.order.size)); }

// Swap row j with the row having the maximum value on column j, while maintaining a permutation matrix P
void pivot(Matrix &A, Permutation& P, uint j) {
    uint best=j; float maximum=abs<float>(A(best,j));
    for(uint i: range(j,A.m)) { // Find biggest on or below diagonal
        float a = A(i,j); //TODO: sparse column iterator
        float value = abs(a);
        if(value>maximum) best=i, maximum=value;
    }
    assert(maximum,A);
    if(best != j) { //swap rows i <-> j
        //for(uint k: range(A.n)) swap(A(best,k),A(j,k)); //TODO: sparse swap
        for(uint k: range(A.n)) { float t=A(best,k); A(best,k)=(float)A(j,k); A(j,k)=t; }
        P.swap(best,j);
    }
}

PLU factorize(Matrix&& A) {
    assert(A.m==A.n);
    uint n = A.n;
    Permutation P(n);
    pivot(A, P, 0); // pivot first column
    float d = 1/A(0,0); for(Matrix::Element& e: A[0](1,n)) e.value *= d;
    // compute an L column, pivot to interchange rows, compute an U row.
    for(uint j: range(1,n-1)) {
        for(uint i: range(j,n)) { // L column
            float sum = 0;
            for(uint k: range(j)) sum += A(i,k)*A(k,j);
            //for(const Matrix::Element& e: A[i])  sum += e.value * A(e.column,j);
            A(i,j) -= sum;
        }
        pivot(A, P, j);
        float d = 1/A(j,j);
        for(uint k: range(j+1,n)) { //U row
            float sum = 0;
            for(uint i: range(j)) sum += A(j,i)*A(i,k); //TODO: sparse row iterator
            A(j,k) = (A(j,k)-sum)*d;
        }
    }
    // compute last L element
    float sum = 0;
    for(uint k: range(n-1)) sum += A(n-1,k)*A(k,n-1); //TODO: sparse row iterator
    A(n-1,n-1) -= sum;
    return __( move(P), move(A) );
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
        for(uint j: range(i)) x[i] -= LU(i,j)*x[j]; //TODO: sparse row iterator
        x[i] = x[i]*(1/LU(i,i));
    }
    for(int i=n-2;i>=0;i--) { // Backward substition from packed U
        for(uint j: range(i+1,n)) x[i] -= LU(i,j) * x[j]; //TODO: sparse row iterator
        //implicit ones on diagonal -> no division
    }
    return x;
}

Vector solve(const Matrix& A, const Vector& b) {
    multi(P,LU, = factorize(copy(A)); ) //compute P,LU
    Vector x = solve(P,LU,b);
    return x;
}

/*Matrix inverse(const Matrix &A) {
    multi(P,LU, = factorize(copy(A)); ) //compute P,LU
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
}*/

/*LU unpack(Matrix&& LU) {
    assert(LU.m==LU.n);
    Matrix L = move(LU);
    Matrix U(L.m,L.n);
    for(uint i: range(L.m)) {
        for(uint j: range(i)) U(i,j) = 0;
        U(i,i) = 1;
        for(uint j: range(i+1,L.n)) U(i,j)=move(L(i,j)), L(i,j) = 0;
    }
    return __( move(L), move(U) );
}*/
