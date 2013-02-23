/// \file sparse.h (TODO:sparse) linear algebra
#include "process.h"
#include "algebra.h"

struct SparseTest {
    SparseTest() {
        const Matrix A ((float[]){4,3,6,3});
        log(A);
        multi( P,LU, = factorize(copy(A)); ) //compute P,LU
        log(determinant(P,LU));
        multi( L,U, = unpack(copy(LU)); ) //unpack LU -> L,U
        log(L);
        log(U);
        log(P);
        if(A!=P*(L*U)) log(A),log(L*U),log(P*(L*U)),assert(A==P*(L*U));
    }
} test;
