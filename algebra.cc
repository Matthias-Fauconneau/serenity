#include "algebra.h"
#include "string.h"

Matrix::Matrix(ref<ref<real>> elements) : Matrix(elements.size, elements[0].size) {
    for(uint i: range(m)) { assert(elements[i].size==n); for(uint j: range(n)) (*this)(i,j) = elements[i][j]; }
}

String str(const Matrix& A) {
    String s;
    for(uint i: range(A.m)) {
        for(uint j: range(A.n)) {
            s<<ftoa(A(i,j),4)<<'\t';
        }
        if(i<A.m-1) s<<'\n';
    }
    return s;
}
