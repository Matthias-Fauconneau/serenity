#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "string.h"
#include "time.h"
typedef double real;

/// Sparse matrix using compressed column storage (CCS)
struct Matrix {
    Matrix(){}
    Matrix(uint m, uint n):m(m),n(n),columns(n,n,array<Element>()){}

    inline real operator()(uint i, uint j) const {
        assert(i<m && j<n);
        const array<Element>& column = columns[j];
        uint index = column.linearSearch(i);
        if(index<column.size && column[index].row == i) return column[index].value;
        return 0;
    }

    inline real& operator()(uint i, uint j) {
        assert(i<m && j<n);
        array<Element>& column = columns[j];
        uint index = column.linearSearch(i);
        if(index<column.size && column[index].row == i) return column[index].value;
        return column.insertAt(index,{i,0.0}).value;
    }

    uint m=0,n=0; /// row and column count
    struct Element {
        uint row; real value;
        bool operator <(uint row) const { return this->row<row; }
    };
    array<array<Element>> columns;
};
template<> string str(const Matrix& a);

/// Dense vector
typedef buffer<real> Vector;

struct UMFPACK {
    UMFPACK(){}
    UMFPACK(const Matrix& A);

    Vector solve(const Vector& b);

    struct Symbolic : handle<void*> { ~Symbolic(); };
    struct Numeric : handle<void*> { Numeric(){} default_move(Numeric); ~Numeric(); };
    uint m=0,n=0;
    buffer<uint> columnPointers;
    buffer<uint> rowIndices;
    buffer<real> values;
    Numeric numeric;
};
