#include "algebra.h"

// Not using umfpack header as it includes stdlib.h
//#include <suitesparse/umfpack.h> //umfpack
//#include <suitesparse/umfpack.h> //amd
//#include <suitesparse/umfpack.h> //suitesparseconfig
//#include <suitesparse/umfpack.h> //cholmod
//#include <suitesparse/umfpack.h> //colamd
//#include <suitesparse/umfpack.h> //openblas
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
#if DEBUG
    for(real e: b) assert(isNumber(e));// Asserts valid constant
#endif
    Vector x(m);
    umfpack_di_solve(UMFPACK_A, columnPointers.data, rowIndices.data, values.data, (real*)x.data, b.data, numeric.pointer, 0, 0);
    return x;
}
