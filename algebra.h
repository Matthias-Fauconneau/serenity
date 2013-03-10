#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "string.h"
#define NaN __builtin_nan("")

// Profiling
#include "time.h"
extern uint64 rowSlice, constElement, elementRead, elementWrite, elementAdd;

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
            tsc tsc(rowSlice);
            uint startIndex=row.size, stopIndex=row.size;
            for(uint index: range(row.size)) if(row[index].column >= start) { startIndex=index; break; }
            for(uint index: range(startIndex,row.size)) if(row[index].column >= stop) { stopIndex=index; break; }
            return row.mutable_slice(startIndex,stopIndex-startIndex);
        }
        Element* begin(){ return row.begin(); }
        Element* end(){ return row.end(); }
    };
    Row row(uint i) { assert(i<m); return {rows[i],n}; }
    Row operator[](uint i) { return row(i); }

    struct ConstRow {
        const array<Element>& row;
        uint n; // Number of columns (for bound checking)
        ref<Element> operator()(uint start, uint stop) const {
            assert(start<n && stop<=n && start<=stop);
            tsc tsc(rowSlice);
            uint startIndex=row.size, stopIndex=row.size;
            for(uint index: range(row.size)) if(row[index].column >= start) { startIndex=index; break; }
            for(uint index: range(startIndex,row.size)) if(row[index].column >= stop) { stopIndex=index; break; }
            return row.slice(startIndex,stopIndex-startIndex);
        }
        const Element* begin(){ return row.begin(); }
        const Element* end(){ return row.end(); }
    };
    ConstRow row(uint i) const { assert(i<m); return {rows[i],n}; }
    ConstRow operator[](uint i) const { return row(i); }

    float at(uint i, uint j) const {
        assert(i<m && j<n);
        tsc tsc(constElement);
        for(const Element& e: row(i)) if(e.column==j) return e.value;
        return 0;
    }
    float operator()(uint i, uint j) const { return at(i,j); }

    struct ElementRef {
        array<Element>& row;
        uint j;
        operator float() const {
            tsc tsc(elementRead);
            if(row.size>8) {
                uint index = row.binarySearch(Element{j,0});
                if(index<row.size) {
                    const Element& e = row[index];
                    if(e.column==j) return e.value;
                }
            } else {
                for(const Element& e: row) if(e.column == j) return e.value;
            }
            return 0;
        }
        float operator=(float v) {
            tsc tsc(elementWrite);
            for(Element& e: row) if(e.column == j) return e.value=v;
            if(!v) return v;
            row.insertSorted( Element{j,v} ); // Implicit fill-in
            return v;
        }
        void operator+=(float v) { operator=( operator float() + v); }
        void operator-=(float v) { operator=( operator float() - v); }
    };
    ElementRef at(uint i, uint j) { assert(i<m && j<n); return {rows[i],j}; }
    ElementRef operator()(uint i, uint j) { return at(i,j); }

    uint m=0,n=0; /// row and column count
    array< array<Element> > rows;
};
template<> inline Matrix copy(const Matrix& o) { Matrix t(o.m,o.n); t.rows=copy(o.rows); return move(t); }
template<> string str(const Matrix& a);
Matrix operator*(const Matrix& a,const Matrix& b);
bool operator==(const Matrix& a,const Matrix& b);
inline Matrix identity(uint size) { Matrix I(size,size); for(uint i: range(size)) I(i,i)=1; return I; }

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

/// Compute determinant of a packed PLU matrix (product along diagonal)
float determinant(const Permutation& P, const Matrix& LU);

/// Dense vector
struct Vector {
    default_move(Vector);
    Vector(uint n):data(n,NaN),n(n){}

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
/// Solves PLUx=b
inline Vector solve(const PLU& PLU, const Vector& b) { return solve(PLU.P, PLU.LU, b); }
/// Solves Ax=b using LU factorization
inline Vector solve(Matrix&& A, const Vector& b) { PLU PLU = factorize(move(A)); Vector x = solve(PLU.P,PLU.LU,b); return x; }
/// Solves PLUx[j]=e[j]
Matrix inverse(const Permutation& P, const Matrix &LU);
inline Matrix inverse(const PLU& PLU) { return inverse(PLU.P, PLU.LU); }
