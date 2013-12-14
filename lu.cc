#include "lu.h"
#include "math.h"

/// Permutation matrix
struct Permutation {
    int even=1; //1 if even count of swaps, -1 if odd count of swaps (used for determinant)
    buffer<int> order;

    Permutation(int n) : order(n) { order.size=n; for(uint i: range(n)) order[i] = i; } // identity ordering
    void swap(int i, int j) { ::swap(order[i],order[j]); even=-even; }
    int determinant() const { return even; }
    int operator[](int i) const { return order[i]; } //b[P[i]] = (P*b)[i]
};

/// Swap row j with the row having the largest value on column j, while maintaining a permutation matrix P
inline void pivot(Matrix& A, Permutation& P, uint j) {
    uint best=j; float maximum=abs<float>(A(best,j));
    for(uint i: range(j,A.m)) { // Find biggest on or below diagonal
        float value = abs<float>(A(i,j));
        if(value>maximum) best=i, maximum=value;
    }
    assert(maximum);
    if(best != j) { //swap rows i <-> j
        for(uint k: range(A.n)) swap(A(best,k),A(j,k));
        P.swap(best,j);
    }
}

struct PLU { Permutation P; Matrix LU; };
/// Factorizes any matrix as the product of a lower triangular matrix and an upper triangular matrix
/// \return permutations (P) and packed LU (U's diagonal is 1).
PLU factorize(Matrix&& LU) {
    const Matrix& A = LU; //const access to LU
    assert(A.m==A.n);
    uint n = A.n;
    Permutation P(n);
    pivot(LU, P, 0); // Pivots rows on first column
    float d = 1/A(0,0); for(uint j: range(1,n)) LU(0,j) *= d;
    for(uint j: range(1,n-1)) {
        // Computes an L column
        for(uint i: range(j,n)) {
            float sum = 0;
            for(uint k: range(j)) sum += A(i,k) * A(k,j);
            if(sum) LU(i,j) = LU(i,j) - sum; // l[ij] = a[ij] - Σ( l[ik]·u[kj] )
        }
        // Pivots to interchange rows
        pivot(LU, P, j);
        // Computes an U row
        float d = 1/A(j,j);
        for(uint i: range(j+1,n)) {
            float sum = 0;
            for(uint k: range(j)) sum += A(j,k) * A(k,i);
            float a = (A(j,i)-sum)*d;
            if(a) LU(j,i) = a; // u[ij] = ( a[ij] - Σ( l[ik]·u[kj] ) ) / l[jj]
        }
    }
    // Computes last L element
    float sum = 0;
    for(uint k: range(n-1)) sum += A(n-1,k) * A(k,n-1);
    LU(n-1,n-1) = LU(n-1,n-1) - sum;
    return {move(P), move(LU)};
}

/// Compute determinant of a packed PLU matrix (product along diagonal)
float determinant(const Permutation& P, const Matrix& LU) {
    float det = P.determinant();
    for(uint i: range(LU.n)) det *= LU(i,i);
    return det;
}

/// Solves PLUx=b
Vector solve(const Permutation& P, const Matrix& LU, const Vector& b) {
    //assert(determinant(P,LU),"Coefficient matrix is singular"_);
    uint n=LU.n;
    Vector x(n);
    for(uint i: range(n)) x[i] = b[P[i]]; // Reorder b in x
    for(uint i: range(n)) { // Forward substitution from packed L
        for(uint j: range(i)) x[i] -= LU(i,j) * x[j];
        x[i] = x[i] / LU(i,i);
    }
    for(int i=n-2;i>=0;i--) { // Backward substition from packed U
        for(uint j: range(i+1,n)) x[i] -= LU(i,j) * x[j];
        //implicit ones on diagonal -> no division
    }
    return x;
}

Vector solve(Matrix&& A, const Vector& b) {
    PLU plu = factorize(move(A));
    return solve(plu.P,plu.LU,b);
}

Matrix inverse(const Permutation& P, const Matrix &LU) {
    uint n = LU.n;
    Matrix A_1(n,n);
    for(uint j=0;j<n;j++) {
        Vector e(n); for(uint i=0;i<n;i++) e[i] = 0; e[j] = 1;
        Vector x = solve(P,LU,move(e));
        for(uint i=0;i<n;i++) A_1(i,j) = move(x[i]);
    }
    return A_1;
}

Matrix inverse(Matrix&& A) {
    PLU plu = factorize(move(A));
    return inverse(plu.P, plu.LU);
}
