#include "process.h"
#include "algebra.h"
#include "window.h"
#include "display.h"
#include "time.h"

struct CFDTest : Widget {
    const uint Mx=64, My=64, N = Mx*My; // Mesh vertex count
    const real H = 1; // Aspect ratio (y/x)
    const real dx = 1./Mx, dy = H/My; // Spatial resolution
    const real dt = 1./60; // Temporal resolution
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
            BCw(i,i+(My-3)*Mx) = 1/(2*dx*dx);
            BCw(i,i+(My-2)*Mx) = -8/(2*dx*dx);
        }
        for(uint y: range(My)) { // Vertical boundaries
            uint i = y*Mx;
            // Left
            BCw(i,i+1) = -8/(2*dx*dx);
            BCw(i,i+2) = 1/(2*dx*dx);
            // Right
            BCw(i,i+Mx-3) = 1/(2*dx*dx);
            BCw(i,i+Mx-2) = -8/(2*dx*dx);
        }

        // Dirichlet and Neumann boundary condition for temperature T
        Matrix BCt(N);
        for(uint x: range(1,Mx-1)) { // Horizontal boundaries
            uint i = x;
            // Top
            BCt(i,i+0*Mx) = -3/(2*dy);
            BCt(i,i+1*Mx) = 4/(2*dy);
            BCt(i,i+2*Mx) = -1/(2*dy);
            Gt[i] = 0; // No flux
            // Bottom
            BCt(i,i+(My-3)*Mx) = 1/(2*dy);
            BCt(i,i+(My-2)*Mx) = -4/(2*dy);
            BCt(i,i+(My-1)*Mx) = 3/(2*dy);
            Gt[i] = 0; // No flux
        }
        for(uint y: range(My)) { // Vertical boundaries
            uint i = y*Mx;
            // Left
            BCt(i,i+0) = 1;
            Gt[i] = 1;
            // Right
            BCt(i,i+Mx-1) = 1;
            Gt[i] = 0; //T1=1, T0=0
        }

        // Left-hand side (implicit) of ω,ϕ,T evolution equations
        Matrix Lw = I - Pr*dt/2*PD;
        Matrix Lj = (I-P) + PD;
        Matrix Lt = BCt + P - dt/2*PD;

        // LU factorizations of the implicit operators
        this->Lw = UMFPACK(Lw);
        this->Lj = UMFPACK(Lj);
        this->Lt = UMFPACK(Lt);

        // Right-hand side (explicit)
        Rw = P + Pr*dt/2*PD;
        Rt = P + dt/2*PD;
        Rj = -1*P;
    }
    void subplot(int2 position, int2 size, uint index, uint count, const Vector& field) {
        Image image(Mx, My);
        for(uint y: range(My)) {
            for(uint x: range(Mx)) {
                uint i = y*Mx+x;
                image(x,y) = clip(0, int(field[i]*0xFF), 0xFF); // [0,1] -> [black,white]
                //image(x,y) = byte4(clip(0, int(-u(i)*0xFF), 0xFF), 0, clip(0, int(u(i)*0xFF), 0xFF), 0xFF); [-1,0,1] -> [blue,black,red]
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

        subplot(position, size, 0, 3, Cw);
        subplot(position, size, 1, 3, Cj);
        subplot(position, size, 2, 3, Ct);
    }
} test;
