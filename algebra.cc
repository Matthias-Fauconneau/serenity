#include "algebra.h"

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

UMFPACK::UMFPACK(Matrix&& o) : A(move(o)) {
    Symbolic symbolic; umfpack_di_symbolic(A.m, A.n, A.columnPointers, A.rowIndices.data, A.values.data, &symbolic.pointer, 0, 0);
    umfpack_di_numeric(A.columnPointers, A.rowIndices.data, A.values.data, symbolic.pointer, &numeric.pointer, 0, 0);
}

Vector UMFPACK::solve(const Vector& b) {
    Vector x(A.m);
    umfpack_di_solve(UMFPACK_A, A.columnPointers, A.rowIndices.data, A.values.data, x, b, numeric.pointer, 0, 0);
    return x;
}
