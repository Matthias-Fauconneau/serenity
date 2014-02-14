#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "string.h"
#include "time.h"
#include "math.h"
typedef ref<real> vector;

/// Dense vector
struct Vector : buffer<real> {
    Vector(){}
    /// Allocates and initializes to NaN
    Vector(uint size, real value=nan):buffer<real>(size,size,value){}
};
inline String str(const Vector& a, char separator=' ') { return str((const ref<real>&)a, separator); }
inline Vector operator*(real a, const vector& x) {
    Vector y(x.size);
    for(uint i: range(y.size)) y[i] = a * x[i];
    return y;
}
inline real dot(const vector& a, const vector& b) { assert(a.size == b.size); real s=0; for(uint i: range(a.size)) s+=a[i]*b[i]; return s; }
inline Vector operator+(const vector& a, const vector& b) {
    assert(a.size == b.size);
    Vector v(a.size);
    for(uint i: range(v.size)) v[i] = a[i] + b[i];
    return v;
}
inline Vector operator-(const vector& a, const vector& b) {
    assert(a.size == b.size, a.size, b.size);
    Vector v(a.size);
    for(uint i: range(v.size)) v[i] = a[i] - b[i];
    return v;
}
inline Vector operator*(const vector& a, const vector& b) {
    assert(a.size == b.size);
    Vector v(a.size);
    for(uint i: range(v.size)) v[i] = a[i] * b[i];
    return v;
}

/// Sparse matrix using compressed column storage (CCS)
struct Matrix {
    Matrix(){}
    Matrix(uint n):m(n),n(n){columns.grow(n);}
    Matrix(uint m, uint n):m(m),n(n){columns.grow(n);}

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
        return column.insertAt(index,Element{i,0.0}).value;
    }

    uint m=0,n=0; /// row and column count
    struct Element {
        uint row; real value;
        bool operator <(uint row) const { return this->row<row; }
    };
    array<array<Element> > columns;
};
inline Matrix copy(const Matrix& A) { Matrix t(A.m,A.n); t.columns=copy(A.columns); return t; }
String str(const Matrix& A);

inline Matrix identity(uint size) { Matrix I(size,size); for(uint i: range(size)) I(i,i)=1; return I; }

//TODO: Expression templates
Matrix operator*(real a, const Matrix& A);
Vector operator*(const Matrix& A, const Vector& b);
Matrix operator+(const Matrix& A, const Matrix& B);
Matrix operator-(const Matrix& A, const Matrix& B);

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
