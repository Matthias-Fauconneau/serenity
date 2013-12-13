#include "thread.h"
#include "layout.h"
#include "window.h"
#include "matrix.h"
#include "algebra.h"
#include "eigen.h"
#include "lu.h"

/// Solves 1D Helmholtz problems with general Robin boundary conditions
struct Spectral {
#if UI
    VList<Plot> plots;
    Window window {&plots, int2(-1), "Spectral"};
    Spectral() {
        window.backgroundColor=window.backgroundCenter=1;
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
#else
    Spectral() {
#endif
        /// Parameters
        const uint n = 10; // Number of points / resolution (~ cut-off frequency)
        const float w = 1;
        const float nu = 1;
        const float H = 1./nu; // Helmholtz coefficient
        const float dt = 1;

        /// Operators
        // Discrete Chebyshev transform operator (Gauss-Lobatto)
        Matrix T(n);
        for(uint i: range(n)) {
            float ci = (i==0||i==n-1) ? 2 : 1;
            for(uint j: range(n)) {
                float cj = (j==0||j==n-1) ? 2 : 1;
                T(i,j) = (i%2?-1:1) * 2 * cos( i*j*PI/(n-1) ) / ((n-1)*ci*cj);
            }
        }
        // Inverse transform (Gauss-Lobatto)
        Matrix iT(n);
        for(uint i: range(n)) for(uint j: range(n)) iT(i,j) = (j%2?-1:1) * cos(i*j*PI/(n-1));
        // Derivation operator in the Chebyshev spectral space (on Gauss-Lobatto nodes)
        Matrix Ds(n);
        for(uint i: range(n)) {
            float ci = (i==0||i==n-1) ? 2 : 1;
            for(uint j: range(n)) Ds(i,j) = j>i && (j+i)%2 ? 2 * j / ci : 0;
        }
        // Derivation operator in the physical space
        Matrix Dx = iT*Ds*T;

        /// System equation
        Matrix L = Dx*Dx;
        // Imposition of the Dirichlet boundary conditions
        L(0,      0) += 1;
        L(n-1,n-1) += 1;
        // Resolution by diagonalization of the interior operator
        Mat2 iP = Mat2( L(0,0), L(0,n-1), L(n-1,0), L(n-1,n-1) ).inverse();
        // Interior operator
        Matrix Li(n-2);
        for(uint i: range(1,n-1)) for(uint j: range(1,n-1)) Li(i-1,j-1) = L(i ,j) - dot(Vec2(L(i,0), L(i,n-1)), iP * Vec2(L(0,j),L(n-1,j)));
        // Diagonalization
        auto E = EigenDecomposition( Li ); // Q^-1 * eigenvalues * Q = Li
        const Vector& eigenvalues = E.eigenvalues; // Eigenvalues are all real and negative
        const Matrix& Q = E.eigenvectors;
        // Inversion including filtering of spurious modes and Helmholtz coefficient
        Vector eigenvalueReciprocals(n-2);
        const real lowestReliableEigenvalue = min(apply(eigenvalues, abs<real>));
        for(uint i: range(n-2)) eigenvalueReciprocals[i] = abs(eigenvalues[i]) <= lowestReliableEigenvalue ? (H ? 1./sq(H) : 0) : 1./(eigenvalues[i] - H*H);

        Vector x(n); for(uint i: range(n)) x[i] = -cos(PI*i/(n-1)); // Gauss-Lobatto collocation points on [-1, 1]
        Vector v(n); for(uint i: range(n)) v[i] = i==0 ? sin(w*0) : 0; // v(x=0,t) = sin(wt), v(t=0,x≠0) = 0
        Vector pNL(n), pB(n); pNL.clear(); pB.clear(); // Previous non-linear, constant term (assumes v[-1]=0)
        for(uint t: range(1)) {
            Vector NL = v*(Dx*v); // Non-linear term
            // Constant term
            Vector b = pB + dt/(2*nu)*(pNL + -3*NL);
            // Source term (from boundary conditions)
            b[0] += dt/(2*nu)*sin(w*t); // v(x=0,t) = sin(wt)
            // Interior constant term
            Vector bi(n-2);
            for(uint i: range(1,n-1)) bi[i-1] = b[i] - dot(Vec2(L(i,0), L(i, n-1)), iP * Vec2(b[0], b[n-1]));
            // Interior solution
            Vector vi = Q * (eigenvalueReciprocals * solve(Q, bi));
            // Solution at boundaries
            Vec2 bb = 0;
            for(uint i: range(n-2)) bb += vi[i] * Vec2(L(0,1+i), L(n-1,1+i));
            Vec2 vb = iP * (Vec2(b[0], b[n-1]) - bb);
            // Total solution
            v[0   ] = vb[0];
            for(uint i: range(1,n-1)) v[i] = vi[i-1];
            v[n-1] = vb[1];

            log(v);
            //plot(x,u);
            pNL = move(NL);
        }
    }
} app;
