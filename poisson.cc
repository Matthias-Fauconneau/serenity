#include "process.h"
#include "algebra.h"
#include "window.h"
#include "display.h"

struct PoissonSolver : Widget {
    // Parameters
    const uint Mx=9, My=9; // Spatial resolutions
    const float Lx=1, Ly=1; // Physical dimensions
    const float dx = Lx/Mx, dy = Ly/My; // Physical resolutions
    const float T0 = 0, T1 = 1; // Lateral boundary conditions
    Image image;
    Window window __(this, int2(-1,-1), "Poisson"_);
    PoissonSolver() {
        const uint N = Mx*My; // Total sample count
        Matrix A (N,N); // Poisson operator
        Vector b (N); // Right-hand vector
        for(uint x: range(Mx)) {
            for(uint y: range(My)) {
                uint i = y*Mx+x;
                if(x==0||x==Mx-1) { // Fixed value boundary condition (u=T1/T0)
                    A(i,i) = 1;
                    b(i) = (x==0 ? T1 : T0);
                }
                else if(y==0||y==My-1) { // Fixed derivative boundary condition (u'=0)
                    if(y==0) {
                        A(i,i+0*My) = -3/(2*dx); A(i,i+1*My) = 4/(2*dx); A(i,i+2*My) = -1/(2*dx);
                    } else {
                        A(i,i+0*My) = 3/(2*dx); A(i,i-1*My) = -4/(2*dx); A(i,i-2*My) = 1/(2*dx);
                    }
                    b(i) = 0; // No flux
                }
                else { // Poisson equation on interior points (using 2nd order finite differences)
                    A(i,i-1) = A(i,i+1) = 1/(dx*dx);
                    A(i,i-My) = A(i,i+My) = 1/(dy*dy);
                    A(i,i) = -2*(1/(dx*dx)+1/(dy*dy));

                    b(i) = 0; // No source
                }
            }
        }
        //log(A);
        //log(b);

        multi(P,LU, = factorize(copy(A)); ) //compute P,LU
        multi(L,U, = unpack(copy(LU)); ) //unpack LU -> L,U
        //log(L); log(U);

        Vector u = solve(A,b);

        // Visualization
        image = Image(Mx, My);
        for(uint x: range(Mx)) {
            for(uint y: range(My)) {
                //image(x,y) = LU(x,y) == 0 ? 0 : 0xFF; // Operator coefficients [0,non zero] -> [black, white]
                uint i = y*Mx+x;
                image(x,y) = clip(0, int(u(i)*0xFF), 0xFF); // [0,1] -> [black,white]
                //image(x,y) = byte4(clip(0, int(-u(i)*0xFF), 0xFF), 0, clip(0, int(u(i)*0xFF), 0xFF), 0xFF); [-1,0,1] -> [blue,black,red]
            }
        }
        image = resize(image, 16*Mx, 16*My);
        window.localShortcut(Escape).connect(&exit);
    }
    int2 sizeHint() { return int2(16*Mx, 16*My); }
    void render(int2 position, int2) { blit(position, image); }
} test;
