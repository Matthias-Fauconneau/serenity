#include "algebra.h"

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

extern "C" {
uint umfpack_di_symbolic(uint m, uint n, const uint* columnPointers, const uint* rowIndices, const double* values, void** symbolic,
                         const double* control, double* info);
void umfpack_di_free_symbolic(void** symbolic);
uint umfpack_di_numeric(const uint* columnPointers, const uint* rowIndices, const double* values, void* symbolic, void** numeric,
                        const double* control, double* info);
void umfpack_di_free_numeric(void** numeric);
enum System { UMFPACK_A };
uint umfpack_di_solve(System sys, const uint* columnPointers, const uint* rowIndices, const double* values, double* X, const double* B,
                      void* numeric, const double* control, double* info);
}

UMFPACK::Symbolic::~Symbolic(){ umfpack_di_free_symbolic(&pointer); }
UMFPACK::Numeric::~Numeric(){ umfpack_di_free_numeric(&pointer); }

UMFPACK::UMFPACK(const Matrix& A):m(A.m),n(A.n){
    // Converts columns to packed storage
    columnPointers = buffer<uint>(n+1,n+1);
    uint nnz=0;
    for(uint j: range(A.n)) {
        columnPointers[j] = nnz;
        nnz += A.columns[j].size;
    }
    columnPointers[A.n] = nnz;
    rowIndices = buffer<uint>(nnz);
    values = buffer<real>(nnz);
    uint index=0;
    for(const array<Matrix::Element>& a: A.columns) {
        for(const Matrix::Element& e: a) {
            rowIndices[index]=e.row;
            values[index]=e.value;
            index++;
        }
    }
    Symbolic symbolic;
    {ScopeTimer t("symbolic"_); umfpack_di_symbolic(m, n, columnPointers, rowIndices, values, &symbolic.pointer, 0, 0); }
    {ScopeTimer t("numeric"_); umfpack_di_numeric(columnPointers, rowIndices, values, symbolic.pointer, &numeric.pointer, 0, 0); }
}

Vector UMFPACK::solve(const Vector& b) {
    Vector x(m);
    umfpack_di_solve(UMFPACK_A, columnPointers, rowIndices.data, values, x, b, numeric.pointer, 0, 0);
    return x;
}
