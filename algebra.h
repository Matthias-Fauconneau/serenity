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
