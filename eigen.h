#pragma once
#include "algebra.h"

struct EigenDecomposition {
    EigenDecomposition(const Matrix& A);
    Vector eigenvalues;
    Matrix eigenvectors;
};
