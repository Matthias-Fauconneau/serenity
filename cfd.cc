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
    const uint Mx=65, My=65, N = Mx*My; // Mesh vertex count
    const real H = 1; // Aspect ratio (y/x)
    const real dx = 1./Mx, dy = H/My; // Spatial resolution
    const real dt = 1./16384; // Temporal resolution //TODO: automatic
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
        window.clearBackground = false;
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
            Gt[i+Mx-1] = 0;
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
        /*for(uint x: range(Mx)) {
            for(uint y: range(My)) {
                uint i = y*Mx+x;
                Ct[i] = 1-real(x)/Mx;
            }
        }*/
    }
    template<uint sqrt> void subplot(int2 position, int2 size, uint index, const Vector& field) {
        int2 subSize = size/int(sqrt);
        Image image = clip(framebuffer, position+int2(index%sqrt,index/sqrt)*subSize, subSize);
        real max=0; for(real v: field) max=::max(max, abs(v));
        constexpr uint scale = 1024/64/sqrt;
        for(uint y: range(My-1)) {
            for(uint x: range(Mx-1)) {
                float v00 = field[(y+0)*Mx+(x+0)]/max;
                float v01 = field[(y+0)*Mx+(x+1)]/max;
                float v10 = field[(y+1)*Mx+(x+0)]/max;
                float v11 = field[(y+1)*Mx+(x+1)]/max;
                for(uint dy: range(scale)) {
                    for(uint dx: range(scale)) {
                        float u=dx/float(scale), v=dy/float(scale);
                        float value = ( v00 * (1-u) + v01 * u) * (1-v)
                                         + ( v10 * (1-u) + v11 * u) * v;
                        uint8 c  = sRGB[ clip<int>(0, round(0xFF*abs(value)), 0xFF) ]; // linear to gamma (sRGB)
                        image(x*scale+dx,y*scale+dy) = value>0 ? byte4(0,0,c,0xFF) : byte4(c,0,0,0xFF); //[-max,0,max] -> [blue,black,red]
                    }
                }
            }
        }
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

        //TODO: legend
        subplot<2>(position, size, 0, Cw);
        subplot<2>(position, size, 1, Cj);
        subplot<2>(position, size, 2, Ct);
        //TODO: |u|, flow lines
        static int t=0; if(t++<4096) window.render(); else log("Stopped");
    }
} test;
