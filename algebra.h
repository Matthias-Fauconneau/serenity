#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "string.h"
#include "time.h"
#include "math.h"
typedef ref<real> vector;
typedef buffer<real> Vector;

/// Sparse matrix using compressed column storage (CCS)
struct Matrix {
    uint m=0,n=0; /// row and column count
    struct Element {
        uint row; real value;
        bool operator <(uint row) const { return this->row < row; }
    };
    buffer<array<Element>> columns;

    Matrix(){}
    Matrix(uint n) : m(n), n(n), columns{n} { columns.clear();}
    Matrix(uint m, uint n) : m(m), n(n), columns{n} { columns.clear();}
    void clear() { for(auto& column: columns) column.clear(); }

    inline real operator()(uint i, uint j) const {
        assert(i<m && j<n);
        const array<Element>& column = columns[j];
        size_t index = column.binarySearch(i);
        if(index<column.size && column[index].row == i) return column[index].value;
        return 0;
    }

    inline real& operator()(uint i, uint j) {
        assert(i<m && j<n);
        array<Element>& column = columns[j];
        size_t index = column.binarySearch(i);
        if(index<column.size && column[index].row == i) return column[index].value;
        return column.insertAt(index,Element{i, 0.0}).value;
    }
};

struct UMFPACK {
    UMFPACK(){}
    UMFPACK(const Matrix& A);

    Vector solve(const Vector& b);

    struct Symbolic : handle<void*> { ~Symbolic(); };
    struct Numeric : handle<void*> { Numeric(){} default_move(Numeric); ~Numeric(); };
    uint m=0,n=0;
    buffer<int> columnPointers;
    buffer<int> rowIndices;
    buffer<real> values;
    Numeric numeric;
};

/*#include <cholmod.h>
struct CholMod {
    CholMod(){}
    CholMod(const Matrix& A) {
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

        cholmod_start(&c);
        cA.nrow = n;
        cA.ncol = m;
        cA.nzmax = nnz;
        cA.p = columnPointers.data;
        cA.i = rowIndices.data
        cA.nz = 0;
        cA.x = values.data;
        cA.z = 0;
        stype;
        itype=CHOLMOD_INT;
        xtype = real;
        dtype= float;
        int sorted = true;
        int packed = true;
        cholmod_factor* L = cholmod_analyze (cA, &c);
        cholmod_factorize (cA, L, &c);
    }
    ~CholdMod() {
        cholmod_free_factor (&L, &c) ;
        cholmod_finish (&c);
    }

    Vector solve(const Vector& b) {
        Vector x(m);
        x = cholmod_solve (CHOLMOD_A, L, b, &c);
    }

    uint m=0,n=0;
    buffer<int> columnPointers;
    buffer<int> rowIndices;
    buffer<real> values;
    cholmod_common c;
    cholmod_sparse cA;
};*/
