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
        bool operator<(const Element& o) const { return column<o.column; }
    };

    struct Row {
        array<Element>& row;
        uint n; // Number of columns (for bound checking)
        mutable_ref<Element> operator()(uint start, uint stop) const {
            assert(start<n && stop<=n && start<stop);
            uint startIndex=0, stopIndex=0;
            for(uint index: range(row.size)) if(row[index].column >= start) { startIndex=index; break; }
            for(uint index: range(startIndex,row.size)) if(row[index].column >= stop) { stopIndex=index; break; }
            return row.mutable_slice(startIndex,stopIndex);
        }
        Element* begin(){ return row.begin(); }
        Element* end(){ return row.end(); }
    };
    Row row(uint i) { assert(i<m); return __(rows[i],n); }
    Row operator[](uint i) { return row(i); }

    struct ConstRow {
        const array<Element>& row;
        uint n; // Number of columns (for bound checking)
        ref<Element> operator()(uint start, uint stop) const {
            assert(start<n && stop<=n && start<=stop);
            uint startIndex=0, stopIndex=0;
            for(uint index: range(row.size)) if(row[index].column >= start) { startIndex=index; break; }
            for(uint index: range(startIndex,row.size)) if(row[index].column >= stop) { stopIndex=index; break; }
            return row.slice(startIndex,stopIndex);
        }
        const Element* begin(){ return row.begin(); }
        const Element* end(){ return row.end(); }
    };
    ConstRow row(uint i) const { assert(i<m); return __(rows[i],n); }
    ConstRow operator[](uint i) const { return row(i); }

    float at(uint i, uint j) const {
        assert(i<m && j<n);
        for(const Element& e: row(i)) if(e.column==j) return e.value;
        return 0;
    }
    float operator()(uint i, uint j) const { return at(i,j); }

    struct ElementRef {
        array<Element>& row;
        uint j;
        operator float() const {
            for(const Element& e: row) if(e.column == j) return e.value;
            return 0;
        }
        float operator=(float v) {
            for(Element& e: row) if(e.column == j) return e.value=v;
            // Implicit fill-in
            if(!v) return v; //TODO: assert(v) ?
            row.insertSorted( Element __(j,v) );
            return v;
        }
        void operator+=(float v) { operator=( operator float() + v); }
        void operator-=(float v) { operator=( operator float() - v); }
    };
    ElementRef at(uint i, uint j) { assert(i<m && j<n); return __(rows[i],j); }
    ElementRef operator()(uint i, uint j) { return at(i,j); }

    uint m=0,n=0; /// row and column count
    array< array<Element> > rows;
};

/// Permutation matrix
struct Permutation {  
    int even=1; //1 if even count of swaps, -1 if odd count of swaps (used for determinant)
    array<int> order;

    Permutation(int n) : order(n,n) { for(uint i: range(n)) order[i] = i; } // identity ordering
    void swap(int i, int j) { ::swap(order[i],order[j]); even=-even; }
    int determinant() const { return even; }
    int operator[](int i) const { return order[i]; } //b[P[i]] = (P*b)[i]
};

/// Swap row j with the row having the largest value on column j, while maintaining a permutation matrix P
void pivot(Matrix &A, Permutation& P, uint j);

/// Factorizes any matrix as the product of a lower triangular matrix and an upper triangular matrix
/// \return permutations (P) and packed LU (U's diagonal is 1).
struct PLU { Permutation P; Matrix LU; };
PLU factorize(Matrix&& A);

/// Convenience macro to extract multiple return arguments
#define multi(A, B, F) auto A##B_ F auto A = move(A##B_.A); auto B=move(A##B_.B);

/// Compute determinant of a packed PLU matrix (product along diagonal)
float determinant(const Permutation& P, const Matrix& LU);

/// Dense vector
struct Vector {
    default_move(Vector);
    Vector(uint n):data(n,n,NaN),n(n){}

    const float& at(uint i) const { assert(data && i<n); return data[i]; }
    float& at(uint i) { assert(data && i<n); return data[i]; }
    const float& operator[](uint i) const { return at(i); }
    float& operator[](uint i) { return at(i); }
    const float& operator()(uint i) const { return at(i); }
    float& operator()(uint i) { return at(i); }

    buffer<float> data; /// elements stored in column-major order
    uint n=0; /// component count
};

/// Solves PLUx=b
Vector solve(const Permutation& P, const Matrix& LU, const Vector& b);
inline Vector solve(const PLU& PLU, const Vector& b) { return solve(PLU.P, PLU.LU, b); }

/// Solves Ax=b using LU factorization
Vector solve(Matrix&& A, const Vector& b);
