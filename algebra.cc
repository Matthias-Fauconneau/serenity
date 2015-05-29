#include "algebra.h"
#undef packed
#include <cholmod.h> // cholmod suitesparseconfig colamd openblas amd

static cholmod_common c;
__attribute((constructor(1001))) void cholmod_c() { cholmod_start(&c); }
__attribute((destructor(1001))) void cholmod_d() { cholmod_finish(&c); }

Matrix::Matrix() {
    cA.nz = 0; // TODO
    cA.z = 0;
    cA.stype = -1;
    cA.itype = CHOLMOD_INT;
    cA.xtype = CHOLMOD_REAL;
    cA.dtype= CHOLMOD_DOUBLE;
    cA.sorted = true;
    cA.packed = true;
}

void Matrix::reset(size_t size) {
    size_t previousSize = columnPointers.size;
    if(columnPointers.size < size+1) {
        columnPointers.grow(size+1);
        cA.p = (void*)columnPointers.data;
        columnPointers.sliceRange(previousSize, size+1).clear(values.size);
        cA.nrow = size;
        cA.ncol = size;
        cA.nzmax = 0;
    }
    // TODO: garbage collect zeroes (contact loss) to reduce artifical fill
    values.mref::clear(0);
}

void Matrix::factorize() {
    cA.i = (void*)rowIndices.data;
    cA.x = (void*)values.data;
    if(cA.nzmax != values.size) { // New values
        cA.nzmax = values.size;
        if(L) cholmod_free_factor(&L, &c);
        L = cholmod_analyze(&cA, &c);
    }
    cholmod_factorize(&cA, L, &c);
}
Matrix::~Matrix() { cholmod_free_factor(&L, &c); }

buffer<double> Matrix::solve(ref<double> b) {
    cholmod_dense src {b.size, 1, b.size, b.size, (void*)b.data, 0, CHOLMOD_REAL, CHOLMOD_DOUBLE};
    cholmod_dense* dst = cholmod_solve(CHOLMOD_A, L, &src, &c);
    buffer<double> x((double*)dst->x, b.size, b.size);
    delete dst;
    return x;
}
