#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "string.h"
#include "time.h"
#include "math.h"
#undef packed
#include <cholmod.h>

/// Sparse matrix using compressed column storage (CCS)
struct Matrix {
    array<int> columnPointers;
    array<int> rowIndices;
    array<double> values;
    cholmod_sparse cA;
    //bool analyze = true;
    struct cholmod_factor_struct* L = 0;

    Matrix();
    ~Matrix();
    void reset(size_t size);

    inline double& operator()(int i, int j) {
        int start = columnPointers[j], end = columnPointers[j+1];
        for(int index: range(start,end))
         if(rowIndices[index]==i) return values[index];
        //values.grow(values.size+1); rowIndices.grow(rowIndices.size+1);
        for(int& pointer: columnPointers.slice(j+1)) pointer++;
        rowIndices.insertAt(end, i);
        return values.insertAt(end, 0.);
    }

    void factorize();
    buffer<double> solve(ref<double> b);
};
