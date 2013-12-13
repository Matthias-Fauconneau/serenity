#include "eigen.h"
#undef unused
#undef Type
#include <eigen3/Eigen/Eigenvalues>

EigenDecomposition::EigenDecomposition(const Matrix& A) : eigenvalues(A.n), eigenvectors(A.n,A.n) {
    assert(A.m == A.n);
    Eigen::Map<Eigen::MatrixXd> a(const_cast<real*>((const real*)A.elements), A.n, A.n); // Column major
    Eigen::EigenSolver<Eigen::MatrixXd> e(a);
    const auto& eigenvalues = e.eigenvalues();
    for(uint i: range(A.n)) {
        if(eigenvalues[i].imag()) log(eigenvalues[i]);
        this->eigenvalues[i] = eigenvalues[i].real();
    }
    auto eigenvectors = e.eigenvectors();
    for(uint i: range(A.n)) for(uint j: range(A.n)) {
        if(eigenvectors(i,j).imag()) log(eigenvectors(i,j));
        this->eigenvectors(i,j) = eigenvectors(i,j).real();
    }
}
