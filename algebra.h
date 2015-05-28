#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "string.h"
#include "time.h"
#include "math.h"
typedef ref<real> vector;
typedef buffer<real> Vector;

/// Sparse matrix using compressed column storage (CCS)
struct Matrix {
    size_t m=0,n=0; /// row and column count
    struct Element {
        uint row; real value;
        bool operator <(uint row) const { return this->row < row; }
    };
    buffer<array<Element>> columns;

    Matrix(){}
    Matrix(size_t n) : m(n), n(n), columns{n} { columns.clear();}
    Matrix(size_t m, size_t n) : m(m), n(n), columns{n} { columns.clear();}
    void clear() { for(auto& column: columns) column.clear(); }

    inline real operator()(size_t i, size_t j) const {
        assert(i<m && j<n);
        const array<Element>& column = columns[j];
        size_t index = column.binarySearch(i);
        if(index<column.size && column[index].row == i) return column[index].value;
        return 0;
    }

    inline real& operator()(size_t i, size_t j) {
        assert(i<m && j<n);
        array<Element>& column = columns[j];
        size_t index = column.binarySearch(i);
        if(index<column.size && column[index].row == i) return column[index].value;
        return column.insertAt(index,Element{uint(i), 0.0}).value;
    }
};

struct UMFPACK {
    UMFPACK(){}
    UMFPACK(const Matrix& A);

    Vector solve(const vector& b);

    struct Symbolic : handle<void*> { ~Symbolic(); };
    struct Numeric : handle<void*> { Numeric(){} default_move(Numeric); ~Numeric(); };
    size_t m=0,n=0;
    buffer<int> columnPointers;
    buffer<int> rowIndices;
    buffer<real> values;
    Numeric numeric;
};

#undef packed
#include <cholmod.h>
struct CholMod {
    CholMod(){}
    CholMod(const Matrix& A) : m(A.m), n(A.n) {
        assert_(A.m == A.n);
        //for(size_t i: range(A.m)) for(size_t j: range(A.n)) assert_(A(i,j)==A(j,i));
        //for(size_t i: range(A.m)) assert_(A(i,i) > 0, i, A(i,i));
        // FIXME: lower only, no copy
        columnPointers = buffer<int>(A.n+1);
        size_t nnz=0;
        for(size_t j: range(A.n)) {
            columnPointers[j] = nnz;
            nnz += A.columns[j].size;
        }
        columnPointers[A.n] = nnz;
        rowIndices = buffer<int>(nnz);
        values = buffer<real/*float*/>(nnz);
        size_t index=0;
        for(const array<Matrix::Element>& column: A.columns) {
            for(const Matrix::Element& e: column) {
                rowIndices[index] = e.row;
                values[index] = e.value;
                index++;
            }
        }

        cholmod_start(&c); // FIXME: once
        c.supernodal = CHOLMOD_SIMPLICIAL;
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
        cA.dtype= CHOLMOD_DOUBLE; //CHOLMOD_SINGLE;
        cA.sorted = true;
        cA.packed = true;
        L = cholmod_analyze(&cA, &c);
        cholmod_factorize(&cA, L, &c);
    }
    ~CholMod() {
        cholmod_free_factor(&L, &c) ;
        cholmod_finish(&c); // FIXME: once
    }

    buffer<real> solve(ref<real> b) {
        cholmod_dense B {b.size, 1, b.size, b.size, (void*)b.data, 0, CHOLMOD_REAL, CHOLMOD_DOUBLE};
        cholmod_dense *dst = cholmod_solve(CHOLMOD_A, L, &B, &c);
        buffer<real> x((real*)dst->x, n, n);
        delete dst;
        return x;
    }
    /*buffer<float> solve(ref<float> b) {
        cholmod_dense src {b.size, 1, b.size, 0, (void*)b.data, 0, CHOLMOD_REAL, CHOLMOD_SINGLE};
        cholmod_dense *dst = cholmod_solve(CHOLMOD_A, L, &src, &c);
        buffer<float> x((float*)dst->x, n, n);
        delete dst;
        return x;
    }*/

    size_t m=0, n=0;
    buffer<int> columnPointers;
    buffer<int> rowIndices;
    buffer<real/*float*/> values;
    cholmod_common c;
    cholmod_factor* L;
};
