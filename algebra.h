#pragma once
/// Numeric linear algebra (matrix operations, linear solver)
#include "memory.h"
#include "string.h"
#define NaN __builtin_nan("")

/// Sparse CSR matrix
struct Matrix {
    default_move(Matrix);
    Matrix(uint m, uint n):m(m),n(n),lines(m+1,0){}

    struct Element { uint column; float value; };
    struct Row {
        Matrix& A;
        uint i;
        struct Slice {
            array<Element>& data;
            uint start, stop;
            struct iterator {
                array<Element>& data; uint index;
                Element& operator*() {return data[index];}
                iterator& operator++(){ index++; return *this;}
                bool operator !=(const iterator& o) const{return index<o.index;}
            };
            iterator begin(){ return __(data, start); }
            iterator end(){ return __(data, stop); }
        };
        Slice operator()(uint start, uint stop) const {
            assert(start<A.n && stop<=A.n && start<stop, start, stop, A.n);
            uint startIndex=0, stopIndex=0;
            for(uint index: range(A.lines[i],A.lines[i+1])) if(A.data[index].column >= start) { startIndex=index; break; }
            for(uint index: range(startIndex,A.lines[i+1])) if(A.data[index].column >= stop) { stopIndex=index; break; }
            return __(A.data,startIndex,stopIndex);
        }
        Slice::iterator begin(){ return __(A.data, A.lines[i]); }
        Slice::iterator end(){ return __(A.data, A.lines[i+1]); }
    };
    Row row(uint i) { assert(i<m); return __(*this,i); }
    Row operator[](uint i) { return row(i); }

    struct ConstRow {
        const Matrix& A;
        uint i;
        struct Slice {
            const array<Element>& data;
            uint start, stop;
            struct iterator {
                const array<Element>& data; uint index;
                const Element& operator*() {return data[index];}
                iterator& operator++(){ index++; return *this;}
                bool operator !=(const iterator& o) const{return index<o.index;}
            };
            iterator begin(){ return __(data, start); }
            iterator end(){ return __(data, stop); }
        };
        Slice operator()(uint start, uint stop) const {
            assert(start<A.n && stop<=A.n && start<=stop, start, stop, A.n);
            uint first=A.lines[i], last=A.lines[i+1], startIndex=last, stopIndex=last;
            for(uint index: range(first,last)) if(A.data[index].column >= start) { startIndex=index; break; }
            for(uint index: range(startIndex,last)) if(A.data[index].column >= stop) { stopIndex=index; break; }
            return __(A.data,startIndex,stopIndex);
        }
        Slice::iterator begin(){ return __(A.data, A.lines[i]); }
        Slice::iterator end(){ return __(A.data, A.lines[i+1]); }
    };
    ConstRow row(uint i) const { assert(i<m); return __(*this,i); }
    ConstRow operator[](uint i) const { return row(i); }

    float at(uint i, uint j) const {
        assert(i<m && j<n);
        for(const Element& e: row(i)) if(e.column==j) return e.value;
        return 0;
    }
    float operator()(uint i, uint j) const { return at(i,j); }

    struct ElementRef {
        Matrix& A;
        Element* e;
        uint i,j;
        operator float() const { return e?e->value:0; }
        float operator=(float v) {
            if(!e) { // Implicit fill-in
                if(!v) return v;
                uint index=A.lines[i]; for(; index<A.lines[i+1]; index++) if(A.data[index].column > j) break;
                A.data.insertAt(index, Element __(j,v));
                for(uint index: range(i+1,A.lines.size)) A.lines[index]++;
                return v;
            }
            return e->value=v;
        }
        void operator+=(float v) { operator=( operator float() + v); }
        void operator-=(float v) { operator=( operator float() - v); }
    };
    ElementRef at(uint i, uint j) {
        assert(i<m && j<n);
        for(Element& e: row(i)) if(e.column == j) return __(*this,&e);
        return __(*this,0,i,j);
    }
    ElementRef operator()(uint i, uint j) { return at(i,j); }

    uint m=0,n=0; /// row and column count
    buffer<uint> lines; /// indices of the first element of each line
    array<Element> data; /// elements stored in order
};
//template<> inline Matrix copy(const Matrix& o) { Matrix t(o.m,o.n); t.lines=buffer<uint>(o.lines); t.data=copy(o.data); return move(t); }
//bool operator==(const Matrix& a,const Matrix& b);
//template<> string str(const Matrix& a);

/// Permutation matrix
struct Permutation {  
    int even=1; //1 if even count of swaps, -1 if odd count of swaps (used for determinant)
    buffer<int> order;

    Permutation(int n) : order(n) { order.size=n; for(uint i: range(n)) order[i] = i; } // identity ordering
    void swap(int i, int j) { ::swap(order[i],order[j]); even=-even; }
    int determinant() const { return even; }
    int operator[](int i) const { return order[i]; } //b[P[i]] = (P*b)[i]
};
//Matrix operator *(const Permutation& P, Matrix&& A);
//template<> string str(const Permutation& P);

/// Swap row j with the row having the largest value on column j, while maintaining a permutation matrix P
void pivot(Matrix &A, Permutation& P, uint j);

/// Factorizes any matrix as the product of a lower triangular matrix and an upper triangular matrix
/// \return permutations (P) and packed LU (U's diagonal is 1).
struct PLU { Permutation P; Matrix LU; };
PLU factorize(Matrix&& A);

/// Convenience macro to extract multiple return arguments
#define multi(A, B, F) auto A##B_ F auto A = move(A##B_.A); auto B=move(A##B_.B);

/// Compute determinant of a packed PLU matrix (product along diagonal)
debug( float determinant(const Permutation& P, const Matrix& LU); )

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
//template<> inline string str(const Vector& a) { string s; for(uint i: range(a.n)) { s<<str(a[i]); if(i<a.n-1) s<<' ';} return s; }

/// Solves PLUx=b
Vector solve(const Permutation& P, const Matrix& LU, const Vector& b);
inline Vector solve(const PLU& PLU, const Vector& b) { return solve(PLU.P, PLU.LU, b); }

/// Solves Ax=b using LU factorization
Vector solve(Matrix&& A, const Vector& b);
