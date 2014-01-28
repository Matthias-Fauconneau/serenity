#include "algebra.h"

Vector operator*(real a, const Vector& b) {
    Vector t(b.size);
    for(uint i: range(t.size)) t[i]=a*b[i];
    return t;
}

Vector operator+(const Vector& a, const Vector& b) {
    assert(a.size==b.size); Vector t(a.size);
    for(uint i: range(t.size)) t[i]=a[i]+b[i];
    return t;
}

Vector operator-(const Vector& a, const Vector& b) {
    assert(a.size==b.size); Vector t(a.size);
    for(uint i: range(t.size)) t[i]=a[i]-b[i];
    return t;
}

Vector operator*(const Vector& a, const Vector& b) {
    assert(a.size==b.size); Vector t(a.size);
    for(uint i: range(t.size)) t[i]=a[i]*b[i];
    return t;
}

String str(const Matrix& A) {
    String s("[ "_);
    for(uint i: range(A.m)) {
        if(A.n==1) s<<ftoa(A(i,0),0,0)<<" "_;
        else {
            for(uint j: range(A.n)) {
                s<<ftoa(A(i,j),0,0)<<" "_;
            }
            if(i<A.m-1) s<<"\n  "_;
        }
    }
    s << "]"_;
    return s;
}

Matrix operator*(real a, const Matrix& A) {
    Matrix t = copy(A);
    for(array<Matrix::Element>& column: t.columns) for(Matrix::Element& e: column) e.value *= a;
    return t;
}

Vector operator*(const Matrix& A, const Vector& b) {
    Vector t(b.size);
    for(uint j: range(A.n)) for(const Matrix::Element& e: A.columns[j]) t[e.row] += e.value*b[j]; //t[i] = A[i,j]b[j]
    return t;
}

Matrix operator+(const Matrix& A, const Matrix& B) {
    assert(A.m == B.m && A.n == B.n);
    Matrix t = copy(A);
    for(uint j: range(B.n)) for(const Matrix::Element& e: B.columns[j]) t(e.row,j) += e.value;
    return t;
}

Matrix operator-(const Matrix& A, const Matrix& B) {
    assert(A.m == B.m && A.n == B.n);
    Matrix t = copy(A);
    for(uint j: range(B.n)) for(const Matrix::Element& e: B.columns[j]) t(e.row,j) -= e.value;
    return t;
}

// Not using umfpack headers as it includes stdlib.h
//#include <suitesparse/umfpack.h> //umfpack
extern "C" {
uint umfpack_di_symbolic(uint m, uint n, const int* columnPointers, const int* rowIndices, const double* values, void** symbolic,
                         const double* control, double* info);
void umfpack_di_free_symbolic(void** symbolic);
uint umfpack_di_numeric(const int* columnPointers, const int* rowIndices, const double* values, void* symbolic, void** numeric,
                        const double* control, double* info);
void umfpack_di_free_numeric(void** numeric);
enum System { UMFPACK_A };
uint umfpack_di_solve(System sys, const int* columnPointers, const int* rowIndices, const double* values, double* X, const double* B,
                      void* numeric, const double* control, double* info);
}

UMFPACK::Symbolic::~Symbolic(){ umfpack_di_free_symbolic(&pointer); }
UMFPACK::Numeric::~Numeric(){ umfpack_di_free_numeric(&pointer); }

UMFPACK::UMFPACK(const Matrix& A):m(A.m),n(A.n){
    columnPointers = buffer<int>(n+1,n+1);
    uint nnz=0;
    for(uint j: range(A.n)) {
        columnPointers[j] = nnz;
        nnz += A.columns[j].size;
    }
    columnPointers[A.n] = nnz;
    rowIndices = buffer<int>(nnz);
    values = buffer<real>(nnz);
    uint index=0;
    for(const array<Matrix::Element>& column: A.columns) {
        for(const Matrix::Element& e: column) {
            rowIndices[index]=e.row;
            values[index]=e.value;
            index++;
        }
    }
    Symbolic symbolic;
    umfpack_di_symbolic(m, n, columnPointers.data, rowIndices.data, values.data, &symbolic.pointer, 0, 0);
    umfpack_di_numeric(columnPointers.data, rowIndices.data, values.data, symbolic.pointer, &numeric.pointer, 0, 0);
}

Vector UMFPACK::solve(const Vector& b) {
    Vector x(m);
    umfpack_di_solve(UMFPACK_A, columnPointers.data, rowIndices.data, values.data, (real*)x.data, b.data, numeric.pointer, 0, 0);
    return x;
}
