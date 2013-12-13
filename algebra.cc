#include "algebra.h"
#include "string.h"

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
