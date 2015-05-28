#include "algebra.h"
#undef packed
#include <cholmod.h> // cholmod suitesparseconfig colamd openblas amd

static cholmod_common c;
__attribute((constructor(1001))) void cholmod_c() { cholmod_start(&c); }
__attribute((destructor(1001))) void cholmod_d() { cholmod_finish(&c); }

CholMod::CholMod(const Matrix& A) {
    buffer<int> columnPointers(A.n+1);
    size_t nnz=0;
    for(size_t j: range(A.n)) {
        columnPointers[j] = nnz;
        nnz += A.columns[j].size;
    }
    columnPointers[A.n] = nnz;
    buffer<int> rowIndices(nnz);
    buffer<double> values(nnz);
    size_t index=0;
    for(const array<Matrix::Element>& column: A.columns) {
        for(const Matrix::Element& e: column) {
            rowIndices[index] = e.row;
            values[index] = e.value;
            index++;
        }
    }

    cholmod_sparse cA;
    cA.nrow = A.m;
    cA.ncol = A.n;
    cA.nzmax = nnz;
    cA.p = (void*)columnPointers.data;
    cA.i = (void*)rowIndices.data;
    cA.nz = 0;
    cA.x = (void*)values.data;
    cA.z = 0;
    cA.stype = -1;
    cA.itype = CHOLMOD_INT;
    cA.xtype = CHOLMOD_REAL;
    cA.dtype= CHOLMOD_DOUBLE;
    cA.sorted = true;
    cA.packed = true;
    L = cholmod_analyze(&cA, &c);
    cholmod_factorize(&cA, L, &c);
}
CholMod::~CholMod() {
    cholmod_free_factor(&L, &c) ;
}

buffer<double> CholMod::solve(ref<double> b) {
    cholmod_dense src {b.size, 1, b.size, b.size, (void*)b.data, 0, CHOLMOD_REAL, CHOLMOD_DOUBLE};
    cholmod_dense *dst = cholmod_solve(CHOLMOD_A, L, &src, &c);
    buffer<double> x((double*)dst->x, b.size, b.size);
    delete dst;
    return x;
}
