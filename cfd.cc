#include "process.h"
#include "algebra.h"
#include "window.h"
#include "display.h"
#include "time.h"
#include "interface.h"

/// Displays A as an image in a new window
void spy(const Matrix& A) {
    Image image(A.m, A.n);
    for(uint i: range(A.m)) for(uint j: range(A.n)) image(j,i) = A(i,j) ? 0xFF : 0;
    static ImageView imageView(move(image));
    static Window window(&imageView, int2(-1,-1), "Spy"_);
    window.localShortcut(Escape).connect(&exit);
}

struct CFDTest : Widget {
    const uint Mx=32, My=32, N = Mx*My; // Mesh vertex count
    const real H = 1; // Aspect ratio (y/x)
    const real dx = 1./Mx, dy = H/My; // Spatial resolution
    const real dt = 1./4096; // Temporal resolution
    const real Ra = 1; // Rayleigh
    const real Pr = 1; // Prandtl

    Matrix PDX{N}, PDY{N}; // Partial derivatives ∂x, ∂y

    UMFPACK Lw, Lj, Lt; // Factorized left-hand side (implicit) of ω,ϕ,T evolution equations
    Matrix Rw{N}, Rj{N}, Rt{N}; // Right-hand side (explicit) of ω,ϕ,T evolution equations
    Matrix BCw{N}; // Thom boundary condition for vorticity ω (extrapolate from ϕ)

    Vector Gt{N}; // Right-hand vector for temperature equation (BC and source field)

    Vector Pj{N}, Cj{N}; // Previous and current ϕ field
    Vector Cw{N}; // Current ω field
    Vector Ct{N}; // Current T field

    Vector PNLw{N}, CNLw{N}; // Previous and current non-linear ω term
    Vector PNLt{N}, CNLt{N}; // Previous and current non-linear T term

    Window window {this, int2(1024,1024), "CFDTest"_};
    CFDTest() {
        window.backgroundColor = 0;
        window.localShortcut(Escape).connect(&exit);

        Matrix I = identity(N);
        Matrix P{N}, PD{N}; // Elementary matrices (interior points projector, Laplacian Δ and partial derivatives ∂x, ∂y)
        for(uint x: range(Mx)) {
            for(uint y: range(My)) {
                uint i = y*Mx+x;
                if(x!=0 && x!=Mx-1 && y!=0 && y!=My-1) {
                    P(i,i) = 1;

                    PD(i,i-Mx) = 1/(dy*dy);
                    PD(i,i-1) = 1/(dx*dx);
                    PD(i,i) = -2*(1/(dx*dx)+1/(dy*dy));
                    PD(i,i+1) = 1/(dx*dx);
                    PD(i,i+Mx) = 1/(dy*dy);

                    PDX(i,i-1) = -1/(2*dx);
                    PDX(i,i+1) = -1/(2*dx);

                    PDY(i,i-Mx) = -1/(2*dy);
                    PDY(i,i+Mx) = -1/(2*dy);
                }
            }
        }

        // Thom boundary condition for vorticity ω
        for(uint x: range(1,Mx-1)) { // Horizontal boundaries
            uint i = x;
            // Top
            BCw(i,i+1*Mx) = -8/(2*dx*dx);
            BCw(i,i+2*Mx) = 1/(2*dx*dx);
            // Bottom
            BCw(i+(My-1)*Mx,i+(My-3)*Mx) = 1/(2*dx*dx);
            BCw(i+(My-1)*Mx,i+(My-2)*Mx) = -8/(2*dx*dx);
        }
        for(uint y: range(My)) { // Vertical boundaries
            uint i = y*Mx;
            // Left
            BCw(i,i+1) = -8/(2*dx*dx);
            BCw(i,i+2) = 1/(2*dx*dx);
            // Right
            BCw(i+Mx-1,i+Mx-3) = 1/(2*dx*dx);
            BCw(i+Mx-1,i+Mx-2) = -8/(2*dx*dx);
        }

        // boundary condition for temperature T
        Matrix BCt(N);
        for(uint x: range(1,Mx-1)) { // Constant derivative (Neumann) on horizontal boundaries
            uint i = x;
            // Top
            BCt(i,i+0*Mx) = -3/(2*dy);
            BCt(i,i+1*Mx) = 4/(2*dy);
            BCt(i,i+2*Mx) = -1/(2*dy);
            Gt[i] = 0; // No flux
            // Bottom
            BCt(i+(My-1)*Mx,i+(My-3)*Mx) = 1/(2*dy);
            BCt(i+(My-1)*Mx,i+(My-2)*Mx) = -4/(2*dy);
            BCt(i+(My-1)*Mx,i+(My-1)*Mx) = 3/(2*dy);
            Gt[i] = 0; // No flux
        }
        for(uint y: range(My)) { // Constant value (Dirichlet) on vertical boundaries
            uint i = y*Mx;
            // Left
            BCt(i+0,i+0) = 1;
            Gt[i] = 1;
            // Right
            BCt(i+Mx-1,i+Mx-1) = 1;
            Gt[i] = 0;
        }

        // Left-hand side (implicit) of ω,ϕ,T evolution equations
        Matrix Lw = I - (Pr*dt/2)*PD;
        Matrix Lj = (I-P) + PD;
        Matrix Lt = BCt + P - (dt/2)*PD;

        // LU factorizations of the implicit operators
        this->Lw = UMFPACK(Lw);
        this->Lj = UMFPACK(Lj);
        this->Lt = UMFPACK(Lt);

        // Right-hand side (explicit)
        Rw = P + (Pr*dt/2)*PD;
        Rj = -1*P;
        Rt = P + (dt/2)*PD;

        // Initial temperature
        for(uint x: range(Mx)) {
            for(uint y: range(My)) {
                uint i = y*Mx+x;
                Ct[i] = 1-real(x)/Mx;
            }
        }
    }
    void subplot(int2 position, int2 size, uint index, uint count, const Vector& field) {
        Image image(Mx, My);
        real max=0; for(real v: field) max=::max(max, abs(v));
        for(uint y: range(My)) {
            for(uint x: range(Mx)) {
                uint i = y*Mx+x;
                float v = field[i]/max;
                uint8 c  = sRGB[ clip<int>(0, round(0xFF* abs(v)), 0xFF) ]; // linear to gamma (sRGB)
                image(x,y) = v>0 ? byte4(0,0,c,0xFF) : byte4(c,0,0,0xFF); //[-max,0,max] -> [blue,black,red]
            }
        }
        int sqrt = ceil(::sqrt(count));
        int2 subSize = size/sqrt;
        blit(position+int2(index%sqrt,index/sqrt)*subSize, resize(image, subSize.x, subSize.y));
    }
    void render(int2 position, int2 size) {
        // Solves evolution equations
        Vector Nt  = Lt .solve(Rt*Ct   + (3*dt/2)*CNLt  - (dt/2)*PNLt  + Gt);
        Vector Nw = Lw.solve(Rw*Cw + (3*dt/2)*CNLw - (dt/2)*PNLw + (dt/2)*Ra*Pr*PDX*(Nt+Ct) + BCw*(2*Cj-Pj));
        Vector Nj   = Lj .solve(Rj*Nw);
        // Update references
        Cw=move(Nw), PNLw=move(CNLw);
        Pj=move(Cj), Cj=move(Nj);
        Ct=move(Nt), PNLt=move(CNLt);
        // Computes advection for next step
        CNLw = (PDX*Cj)*(PDY*Cw)-(PDY*Cj)*(PDX*Cw);
        CNLt  = (PDX*Cj)*(PDY*Ct )-(PDY*Cj)*(PDX*Ct );

        subplot(position, size, 0, 4, Cw);
        subplot(position, size, 1, 4, Cj);
        subplot(position, size, 2, 4, Ct);
        static int t=0; if(t++<256) window.render(); else log("Done");
    }
} test;
