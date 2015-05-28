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

struct CholMod {
    CholMod(const Matrix& A);
    ~CholMod();
    buffer<double> solve(ref<double> b);

    struct cholmod_factor_struct* L;
};
