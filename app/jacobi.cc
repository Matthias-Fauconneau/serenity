//#define EIGEN_NO_DEBUG 1
#include <eigen3/Eigen/SVD>
#undef assert

#include "jacobi.h"

static inline Vector fromEigen(const Eigen::VectorXf& X) {
    Vector Y(X.rows());
    for(int i: range(Y.size)) Y[i] = X(i);
    return Y;
}

static inline Matrix fromEigen(const Eigen::MatrixXf& X) {
    Matrix Y(X.rows(), X.cols());
    for(int i: range(Y.M)) for(int j: range(Y.N)) Y(i,j) = X(i,j);
    return Y;
}

USV SVD(const Matrix& A) {
    struct MatrixXf { float* data; __PTRDIFF_TYPE__ M, N; } eigenA {A.begin(), A.M, A.N};
    const Eigen::JacobiSVD svd = Eigen::JacobiSVD(*reinterpret_cast<const Eigen::MatrixXf*>(&eigenA), Eigen::ComputeFullU|Eigen::ComputeFullV);
    return {fromEigen(svd.matrixU()), fromEigen(svd.singularValues()), fromEigen(svd.matrixV())};
}
