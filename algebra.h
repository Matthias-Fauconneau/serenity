#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "string.h"

typedef double real;

/// Sparse matrix using compressed column storage (CCS)
struct Matrix {
    Matrix(){}
    Matrix(uint m, uint n):m(m),n(n),columnPointers(n+1,n+1,0){}

    real operator()(uint i, uint j) const {
        assert(i<m && j<n);
        uint index=columnPointers[j];
        for(; index<columnPointers[j+1]; index++) {
            uint row = rowIndices[index];
            if(row >= i) {
                if(row == i) return values[index];
                break;
            }
        }
        return 0;
    }

    real& operator()(uint i, uint j) {
        assert(i<m && j<n);
        uint index=columnPointers[j];
        for(; index<columnPointers[j+1]; index++) {
            uint row = rowIndices[index];
            if(row >= i) {
                if(row == i) return values[index];
                break;
            }
        }
        for(uint& columnPointer: columnPointers.slice(j+1)) columnPointer++;
        rowIndices.insertAt(index, i);
        return values.insertAt(index, 0);
    }

    uint m=0,n=0; /// row and column count
    buffer<uint> columnPointers;
    array<uint> rowIndices;
    array<real> values;
};
template<> string str(const Matrix& a);

/// Dense vector
typedef buffer<real> Vector;

struct UMFPACK {
    UMFPACK(){}
    UMFPACK(Matrix&& A);

    Vector solve(const Vector& b);

    struct Symbolic : handle<void*> { ~Symbolic(); };
    struct Numeric : handle<void*> { Numeric(){} default_move(Numeric); ~Numeric(); };
    Matrix A;
    Numeric numeric;
};
