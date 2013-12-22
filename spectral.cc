#include "thread.h"
#include "algebra.h"
#include "eigen.h"
#include "lu.h"
#include "plot.h"
#include "window.h"
#include "interface.h"
#include "encoder.h"

/// Solves ∂t v + v·∂x v = ν∂xx v on [-1, 1]
struct Spectral {
    /// Parameters
    static constexpr uint n = 160; // Node count
    const float dt = 1./n; // Time step (~ Courant–Friedrichs–Lewy condition)
    const float nu = 1; // Diffusion coefficient
    const float H = 1./(nu*dt); // Helmholtz coefficient
    const float w = 2*PI / 30; // Velocity source frequency
    Vector x{n}; // Nodes positions

    /// System
    Matrix Dx; // Derivation operator in physical space
    Matrix iLb; // Inverse boundary operator
    Vector eigenvalueReciprocals {n-2};
    Matrix Q; // Eigenvectors
    Matrix boundaryColumns{n-2,2}, boundaryRows{2,n-2}; // Boundary columns and rows

    /// Variables
    uint t = 1;
    Vector pNL{n}, v {n}; // Non-linear term at t-1. solution at t

    Plot plot {x, v, 1};
    Window window {&plot, int2(0,720), "Spectral"};
    Encoder encoder {1280,720};
    Spectral() {
        /// Operators
        for(uint i: range(n)) x[i] = -cos(PI*i/(n-1)); // Gauss-Lobatto nodes on [-1, 1]
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
        // Derivation operator in Chebyshev spectral space (on Gauss-Lobatto nodes)
        Matrix Ds(n);
        for(uint i: range(n)) {
            float ci = (i==0||i==n-1) ? 2 : 1;
            for(uint j: range(n)) Ds(i,j) = j>i && (j+i)%2 ? 2 * j / ci : 0;
        }
        // Derivation operator in physical space
        Dx = iT*Ds*T;

        /// System
        Matrix L = Dx*Dx;
        for(uint i: range(n)) L(0, i)=0, L(n-1, i)=0;
        L(0, 0) = 1; L(n-1,n-1) = 1; // Imposition of Dirichlet boundary conditions
        // Boundary operators
        for(uint i: range(1,n-1)) boundaryColumns(i-1,0) = L(i,0), boundaryColumns(i-1,1) = L(i,n-1);
        for(uint i: range(1,n-1)) boundaryRows(0,i-1) = L(0,i), boundaryRows(1,i-1) = L(n-1,i);
        // Inverse corner operator
        iLb = inverse(Matrix({{L(0,0), L(0,n-1)}, {L(n-1,0), L(n-1,n-1)}}));
        // Interior operator
        Matrix Li(n-2);
        for(uint i: range(1,n-1)) for(uint j: range(1,n-1)) Li(i-1,j-1) = L(i ,j) - dot(Vector(L(i,0), L(i,n-1)), iLb * Vector(L(0,j),L(n-1,j)));
        // Diagonalization
        auto E = EigenDecomposition( Li ); // Q^-1 * eigenvalues * Q = Li
        const Vector& eigenvalues = E.eigenvalues; // Eigenvalues are all real and negative
        // Inversion including Helmholtz coefficient and filtering of spurious modes
        const real lowestReliableEigenvalue = min(apply(eigenvalues, abs<real>));
        for(uint i: range(n-2)) eigenvalueReciprocals[i] = abs(eigenvalues[i]) <= lowestReliableEigenvalue ? 1./H : 1./(eigenvalues[i] - H);
        Q = move(E.eigenvectors);
        v.clear(); pNL.clear(); // v[t<=0] = 0

        if(0) { // Displays
            window.backgroundColor=window.backgroundCenter=1;
            window.localShortcut(Escape).connect([]{exit();});
            window.frameSent.connect(this, &Spectral::step); // Displays time steps as fast as possible
            //window.localShortcut(Key(' ')).connect(this, &Spectral::step); // Displays time steps on user input
            window.show();
        } else { // Records
            encoder.start("spectral"_, true, false);
            while(w*t<=2*2*PI) { // Renders as quickly as possible (no display)
                step();
                encoder.writeVideoFrame(renderToImage(plot, encoder.size()));
            }
        }
    }
    void step() {
        Vector NL = v*(Dx*v); // Non-linear term
        Vector L = Dx*Dx*v + 1/(dt*nu) * v; // Linear term
        Vector b = 1/(2*nu)*(3*NL - pNL) - L; // Constant term
        b[0] = -1/(2*nu)*(sin(w*(t-1))+sin(w*t)); // Source term (v(x=0,t) = sin(wt), v(x=1,t)=0)
        b[n-1] = 0;
        Vector bi = b(1, n-1) - boundaryColumns * (iLb * Vector(b[0], b[n-1])); // Interior constant term
        Vector vi = Q * (eigenvalueReciprocals * solve(Q, bi)); // Interior solution
        Vector vb = iLb * (Vector(b[0], b[n-1]) - boundaryRows * vi); // Solution at boundaries
        // Total solution
        v[0   ] = vb[0];
        for(uint i: range(1,n-1)) v[i] = vi[i-1];
        v[n-1] = vb[1];

        pNL = move(NL);
        t++;
        window.render();
    }
} app;
