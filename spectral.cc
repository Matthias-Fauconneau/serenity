#include "thread.h"
#include "layout.h"
#include "window.h"
#include "matrix.h"
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
        const uint n = 101; // Number of points / resolution (~ cut-off frequency)
        const float H = 0; // Helmholtz coefficient
        const float alpha[2] = {0,0}, beta[2] = {1,1}; // Robin boundary condition coefficients: (a + b·dx)u  = g)

        /// Gauss-Lobatto collocation points on [-1, 1]
        Vector x(n); for(uint i: range(n)) x[i] = -cos(PI*i/(n-1));
        /// Expected solution
        const float k = 2*PI; // Test frequency
        Vector u0(n); for(uint i: range(n)) u0[i] = cos(k*x[i]);

        /// Operators
        // Derivation operator in the Chebyshev spectral space on Gauss-Lobatto nodes
        Matrix Ds(n);
        for(uint i: range(n)) {
            float ci = (i==0||i==n) ? 2 : 1;
            for(uint j: range(n)) if((j+i)%2 != 0) Ds(i,j) = 2 * j / ci;
        }
        // Discrete Chebyshev transform operator (Gauss-Lobatto)
        Matrix T(n);
        for(uint i: range(n)) {
            float ci = (i==0||i==n) ? 2 : 1;
            for(uint j: range(n)) {
                float cj = (j==0||j==n) ? 2 : 1;
                T(i,j) = (i%2?-1:1) * 2 * cos( i*j*PI/(n-1) ) / (n*ci*cj);
            }
        }
        // Inverse transform
        Matrix iT(n);
        for(uint i: range(n)) for(uint j: range(n)) iT(i,j) = (i%2?-1:1) * cos(i*j*PI/(n-1));
        // Derivation operator in the physical space
        Matrix Dx = iT*Ds*T;

        /// System equation
        Matrix L = Dx*Dx;
        // Imposition of the Robin Boundary conditions
        for(uint j: range(n)) L(0   ,j) = beta[0]*Dx(0,   j); L(0,      0) += alpha[0];
        for(uint j: range(n)) L(n-1,j) = beta[1]*Dx(n-1,j); L(n-1,n-1) += alpha[1];
        // Source term definition
        Vector S = (-sq(k) - sq(H))*u0; // Lu = S
        S[0   ] = alpha[0]*u0[0   ] + beta[0] * -k*sin(k*x[0   ]);
        S[n-1] = alpha[1]*u0[n-1] + beta[1] * -k*sin(k*x[n-1]);

#if 1 // Direct linear system resolution
        for(uint i: range(n)) L(i,i) -= H*H;
        Vector u = solve(move(L), S);
#else // Resolution by diagonalization of the interior operator
        Mat2 iP = Mat2( L(0,0), L(0,n-1), L(n-1,0), L(n-1,n-1) ).inverse();
        // Interior operator
        Matrix Li(n-2);
        for(uint i: range(1,n-1)) for(uint j: range(1,n-1)) Li(i-1,j-1) = L(i ,j) - dot(Vec2(L(i,0), L(i,n-1)), iP * Vec2(L(0,j),L(n-1,j)));
        // Interior source
        Vector Si(n-2);
        for(uint i: range(1,n-1)) Si[i-1] = S[i] - dot(Vec2(L(i,0), L(i, n-1)), iP * Vec2(S[0], S[n-1]));
        // Diagonalization
        setExceptions(/*Invalid | Denormal | DivisionByZero | Overflow | Underflow*/0); // Masks underflow
        EigenDecomposition E = Li; // Q^-1 * eigenvalues * Q = Li
        const Vector& eigenvalues = E.eigenvalues; // Eigenvalues are all real and negative
        const Matrix& Q = E.eigenvectors;
        // Inversion including helmholtz coefficient and filtering of spurious modes
        Vector eigenvalueReciprocals(n-2);
        const float reliableThreshold = 0x1p-24; //FIXME
        for(uint i: range(n-2)) eigenvalueReciprocals[i] = abs(eigenvalues[i]-sq(H)) < reliableThreshold ? (H ? 1./sq(H) : 0) : 1./(eigenvalues[i] - H*H);
        // Interior solution
        Vector ui = Q * (eigenvalueReciprocals * solve(Q, Si));
        // Solution at boundaries
        Vec2 v = 0;
        for(uint i: range(n-2)) v += ui[i] * Vec2(L(0,1+i), L(n-1,1+i));
        Vec2 ub = iP * (v +Vec2(S[1], S[n-1]));
        // Total solution
        Vector u(n);
        u[0   ] = ub[0];
        for(uint i: range(1,n-1)) u[i] = ui[i-1];
        u[n-1] = ub[1];
#endif
        log(u);
        //plot(x,u);
        //plot(u-u0);
        //plot(abs(T*(u-u0));
    }
} app;
