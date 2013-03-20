#include "process.h"
#include "algebra.h"
#include "window.h"
#include "display.h"

struct CFDTest : Widget {
    const uint Mx=32, My=32; // Spatial resolutions
    const real H=1; // Aspect ratio (y/x)
    const real dx = 1/Mx, dy = H/My; // Physical resolutions
    const uint N = Mx*My; // Total sample count
    UMFPACK LU; // Factorized operator
    Vector b{N}; // Right-hand vector
    Window window {this, int2(-1,-1), "CFDTest"_};
    CFDTest() {
        window.localShortcut(Escape).connect(&exit);

        Matrix A(N,N); // Poisson operator
        for(uint x: range(Mx)) {
            for(uint y: range(My)) {
                uint i = y*Mx+x;
                if(x==0||x==Mx-1) { // Fixed value boundary condition (u=T1/T0)
                    A(i,i) = 1;
                    b[i] = (x==0 ? 1 : 0); //T1=1, T0=0
                }
                else if(y==0||y==My-1) { // Fixed derivative boundary condition (u'=0)
                    if(y==0) {
                        A(i,i+0*My) = -3/(2*dx); A(i,i+1*My) = 4/(2*dx); A(i,i+2*My) = -1/(2*dx);
                    } else {
                        A(i,i+0*My) = 3/(2*dx); A(i,i-1*My) = -4/(2*dx); A(i,i-2*My) = 1/(2*dx);
                    }
                    b[i] = 0; // No flux
                }
                else { // Poisson equation on interior points (using 2nd order finite differences)
                    A(i,i-1) = A(i,i+1) = 1/(dx*dx);
                    A(i,i-My) = A(i,i+My) = 1/(dy*dy);
                    A(i,i) = -2*(1/(dx*dx)+1/(dy*dy));

                    b[i] = 0; // No source
                }
            }
        }
        LU = UMFPACK(move(A));
    }
    void render(int2 position, int2 size) {
        Image image = clip(framebuffer,position,size);
        Vector u = LU.solve(b);
        log(u);
        for(uint y: range(My)) {
            for(uint x: range(Mx)) {
                uint i = y*Mx+x;
                image(x,y) = clip(0, int(u[i]*0xFF), 0xFF); // [0,1] -> [black,white]
                //image(x,y) = byte4(clip(0, int(-u(i)*0xFF), 0xFF), 0, clip(0, int(u(i)*0xFF), 0xFF), 0xFF); [-1,0,1] -> [blue,black,red]
            }
        }
    }
    int2 sizeHint() { return int2(Mx, My); }
} test;
