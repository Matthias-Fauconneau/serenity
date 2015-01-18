#include "algebra.h"

Matrix transpose(const Matrix& A) {
	Matrix At(A.n, A.m);
	for(size_t i: range(A.m)) for(size_t j: range(A.n)) At(j, i) = (float)A(i, j);
	return At;
}

Matrix operator*(const Matrix& a, const Matrix& b) {
	assert(a.n==b.m);
	Matrix r(a.m,b.n);
	for(uint i: range(r.m)) for(uint j: range(r.n)) for(uint k: range(a.n)) r(i,j) = r(i,j) + a(i,k)*b(k,j);
	return r;
}

bool operator==(const Matrix& a,const Matrix& b) {
	assert(a.m==b.m && a.n==b.n);
	for(uint i: range(a.m)) for(uint j: range(a.n)) if(a(i,j)!=b(i,j)) return false;
	return true;
}

String str(const Matrix& a) {
	array<char> s = copyRef("[ "_);
	for(uint i: range(a.m)) {
		if(a.n==1) s.append(str(a(i,0),1,0)+' ');
		else {
			for(uint j: range(a.n)) {
				s.append(str(a(i,j),1,0)+' ');
			}
			if(i<a.m-1) s.append("\n  "_);
		}
	}
	s.append("]"_);
	return move(s);
}

Vector operator*(const Matrix& A,const Vector& b) {
	Vector Ab (A.m);
	for(size_t i: range(A.m)) { float Abi = 0; for(size_t j: range(A.n)) Abi += A(i, j) * b[j]; Ab[i] = Abi; }
	return Ab;
}

PLU factorize(const Matrix& A) {
	Matrix L (A.m, A.m);
	Matrix U (A.m, A.n);
	Permutation P(A.m); // FIXME: Restore pivot version
	assert(A.m==A.n);
	size_t n = A.n;
	for(size_t i: range(n)) U(i, i) = 1;
	for(size_t j: range(n)) {
		for(size_t i: range(j,n)) {
			float sum = 0;
			for(size_t k: range(j)) sum += L(i, k) * U(k, j);
			L(i, j) = A(i, j) - sum;
		}
		for(size_t i: range(j,n)) {
			float sum = 0;
			for(size_t k: range(j)) sum += L(j, k) * U(k, i);
			U(j, i) = (A(j, i) - sum) / L(j, j);
		}
	}
	return {move(P), move(L), move(U)};
}

float determinant(const Permutation& P, const Matrix& L, const Matrix& unused U) {
	float det = P.determinant();
	for(uint i: range(L.n)) det *= L(i,i);
	return det;
}

Vector solve(const Permutation& P, const Matrix& L, const Matrix& U, const Vector& b) {
	assert_(determinant(P,L,U), "Coefficient matrix is singular"_);
	Vector x(U.m);
	for(uint i: range(U.m)) x[i] = b[P[i]]; // Reorder b in x
	for(uint i: range(U.m)) { // Forward substitution from L
		for(size_t k: range(i)) x[i] -= L(i, k) * x[k];
		x[i] = x[i] / L(i,i);
	}
	for(int i=U.m-2;i>=0;i--) { // Backward substition from U
		for(size_t k: range(i+1, U.n)) x[i] -= U(i, k) * x[k];
		// Implicit ones on diagonal -> no division
	}
	return x;
}
