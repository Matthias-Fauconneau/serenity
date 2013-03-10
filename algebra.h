#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "string.h"
#define NaN __builtin_nan("")

/// Sparse CSR matrix
struct Matrix {
    default_move(Matrix);
    Matrix(uint m, uint n):m(m),n(n),rows(m,m,array<Element>()){}

    struct Element {
        uint column; float value;
        Element(uint column, float value=0):column(column),value(value){}
        bool operator<(const Element& o) const { return column<o.column; }
    };
    float operator()(uint i, uint j) const;

    struct ElementRef {
        array<Element>& row;
        uint j;
        inline operator float() const;
        inline float operator=(float v);
    };
    ElementRef operator()(uint i, uint j) { assert(i<m && j<n); return {rows[i],j}; }

    struct ConstRow {
        const array<Element>& row;
        uint n; // Number of columns (for bound checking)
        ref<Element> operator()(uint stop) const { assert(stop<=n); return row.slice(0, row.binarySearch(stop) ); }
        ref<Element> operator()(uint start, uint stop) const;
        const Element* begin(){ return row.begin(); }
        const Element* end(){ return row.end(); }
    };
    ConstRow operator[](uint i) const { assert(i<m); return {rows[i],n}; }

    struct Row {
        array<Element>& row;
        uint n; // Number of columns (for bound checking)
        mutable_ref<Element> operator()(uint stop) const { assert(stop<=n); return row.mutable_slice(0, row.binarySearch(stop)); }
        inline mutable_ref<Element> operator()(uint start, uint stop) const;
        Element* begin(){ return row.begin(); }
        Element* end(){ return row.end(); }
    };
    Row operator[](uint i) { assert(i<m); return {rows[i],n}; }

    uint m=0,n=0; /// row and column count
    array< array<Element> > rows;
};

inline float Matrix::operator()(uint i, uint j) const {
    assert(i<m && j<n);
    const array<Element>& row = rows[i];
    uint index = row.binarySearch(j);
    if(index<row.size) {
        const Element& e = row[index];
        if(e.column==j) return e.value;
    }
    return 0;
}

inline __attribute((always_inline))/*40ms*/ Matrix::ElementRef::operator float() const {
    uint index = row.binarySearch(j);
    if(index<row.size) { const Element& e = row[index]; if(e.column==j) return e.value; }
    return 0;
}

inline float Matrix::ElementRef::operator=(float v) {
    uint index = row.binarySearch(j);
    if(index<row.size) { Element& e = row[index]; if(e.column==j) return e.value=v; }
    assert(v);
    row.insertAt(index, Element(j,v));
    return v;
}

inline ref<Matrix::Element> Matrix::ConstRow::operator()(uint start, uint stop) const {
    assert(start<n && stop<=n && start<=stop);
    uint startIndex = row.binarySearch(start), stopIndex = row.binarySearch(stop);
    return row.slice(startIndex,stopIndex-startIndex);
}

inline mutable_ref<Matrix::Element> Matrix::Row::operator()(uint start, uint stop) const {
    assert(start<n && stop<=n && start<stop);
    uint startIndex = row.binarySearch(start), stopIndex = row.binarySearch(stop);
    return row.mutable_slice(startIndex,stopIndex-startIndex);
}

template<> inline Matrix copy(const Matrix& o) { Matrix t(o.m,o.n); t.rows=copy(o.rows); return move(t); }
template<> string str(const Matrix& a);
Matrix operator*(const Matrix& a,const Matrix& b);
bool operator==(const Matrix& a,const Matrix& b);
inline Matrix identity(uint size) { Matrix I(size,size); for(uint i: range(size)) I(i,i)=1; return I; }

/// Permutation matrix
struct Permutation {  
    int even=1; //1 if even count of swaps, -1 if odd count of swaps (used for determinant)
    array<int> order;

    Permutation(){}
    Permutation(int n) : order(n,n) { for(uint i: range(n)) order[i] = i; } // identity ordering
    void swap(int i, int j) { ::swap(order[i],order[j]); even=-even; }
    int determinant() const { return even; }
    int operator[](int i) const { return order[i]; } //b[P[i]] = (P*b)[i]
};

/// Factorizes any invertible matrix as the product of a lower triangular matrix and an upper triangular matrix
/// \return permutations (P) and packed LU (U's diagonal is 1).
struct PLU { Permutation P; Matrix LU; };
PLU factorize(Matrix&& A);

/// Compute determinant of a packed PLU matrix (product along diagonal)
float determinant(const Permutation& P, const Matrix& LU);

/// Dense vector
typedef buffer<float> Vector;

/// Solves PLUx=b
Vector solve(const Permutation& P, const Matrix& LU, const Vector& b);
/// Solves PLUx=b
inline Vector solve(const PLU& PLU, const Vector& b) { return solve(PLU.P, PLU.LU, b); }
/// Solves Ax=b using LU factorization
inline Vector solve(Matrix&& A, const Vector& b) { PLU PLU = factorize(move(A)); Vector x = solve(PLU.P,PLU.LU,b); return x; }
/// Solves PLUx[j]=e[j]
Matrix inverse(const Permutation& P, const Matrix &LU);
inline Matrix inverse(const PLU& PLU) { return inverse(PLU.P, PLU.LU); }
