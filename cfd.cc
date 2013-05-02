#include "process.h"
#include "math.h"
#include "algebra.h"
#include "window.h"
#include "display.h"
#include "time.h"
#include "interface.h"
#include "png.h"
#if RECORD
#include "record.h"
#endif

/// Maps [-1,0,1] to [blue,black,red]
byte4 toColor(real x) { return x>0 ? byte4(0,0,sRGB(x),0xFF) : byte4(sRGB(-x),0,0,0xFF); }
/// Maps [0,1] to [red,blue] black body radiation
byte4 positiveToColor(real x) {
    constexpr real red=700, green=546, blue=436, C = 2.82;
    constexpr real Cr=1/red, Cg=1/green, Cb=1/blue;
    constexpr real Tr=1/(C*red), Tg=1/(C*green), Tb=1/(C*blue);
    constexpr real Tm=1/(C*3000), TM=1/(C*190); //1700-27000K
    real T = Tm+x*(TM-Tm);
    const real Ir = Tr*Tr*Tr/(exp(Cr/T)-1);
    const real Ig = Tg*Tg*Tg/(exp(Cg/T)-1);
    const real Ib = Tb*Tb*Tb/(exp(Cb/T)-1);
    const real Imax = max(max(Ir,Ig),Ib);
    return byte4(sRGB(Ib/Imax),sRGB(Ig/Imax),sRGB(Ir/Imax), 0xFF);
}
/// Maps (x,y) to (red,green)
byte4 toColor(real x, real y) { return byte4(0,sRGB(y),sRGB(x),0xFF); }
/// Maps (angle,norm) to (hue, saturation/value)
byte4 vecToColor(real x, real y) {
    float norm = sqrt(x*x+y*y);
    vec3 rgb = HSVtoRGB(atan(y, x)+PI, norm, norm);
    return byte4(sRGB(rgb[2]), sRGB(rgb[1]), sRGB(rgb[0]), 0xFF);
}

/// Converts a field to an sRGB8 image
Image toImage(const buffer<real>& field, uint Mx, uint My, real max) {
    assert(field.size==Mx*My);
    Image image(Mx, My);
    for(uint y: range(My)) for(uint x: range(Mx)) image(x,y) = toColor( field[(y+0)*Mx+(x+0)]/max );
    return image;
}
/// Converts a positive field to an sRGB8 image
Image positiveToImage(const buffer<real>& field, uint Mx, uint My, real max=1) {
    assert(field.size==Mx*My);
    Image image(Mx, My);
    for(uint y: range(My)) for(uint x: range(Mx)) image(x,y) = positiveToColor( field[(y+0)*Mx+(x+0)]/max );
    return image;
}
/// Converts two fields to an sRGB8 image
Image toImage(const buffer<real>& red, const buffer<real>& green, uint Mx, uint My) {
    Image image(Mx, My);
    for(uint y: range(My)) for(uint x: range(Mx)) image(x,y) = toColor( red[(y+0)*Mx+(x+0)], green[(y+0)*Mx+(x+0)] );
    return image;
}
/// Converts a vector field to an sRGB8 image
Image vecToImage(const buffer<real>& X, const buffer<real>& Y, uint Mx, uint My, real max) {
    Image image(Mx, My);
    for(uint y: range(My)) for(uint x: range(Mx)) image(x,y) = vecToColor( X[(y+0)*Mx+(x+0)]/max, Y[(y+0)*Mx+(x+0)]/max );
    return image;
}

/// Displays an image with a label
void subplot(int2 position, int2 size, uint count, uint index, const Image& image, const ref<byte>& title, real max=1) {
    int sqrt = ceil(::sqrt(count));
    int2 subSize = size/sqrt;
    int2 subPosition = position+int2(index%sqrt,index/sqrt)*subSize;
    fill(subPosition+Rect(subSize.x,16),black);
    Text(title+(max!=1?" (max="_+ftoa(max,1,0,true)+")"_:""_),16,white).render(subPosition,int2(subSize.x,16));
    blit(subPosition+int2(0,16), image, subSize-int2(0,16));
}
/// Displays a field with a label
void subplot(int2 position, int2 size, uint count, uint index, const buffer<real>& field, uint Mx, uint My, const ref<byte>& title) {
    real max=0; for(real v: field) max=::max(max, norm(v));
    subplot(position, size, count, index, toImage(field, Mx, My, max), title, max);
}
/// Displays a vector field with a label
void subplot(int2 position, int2 size, uint count, uint index, const buffer<real>& X, const buffer<real>& Y, uint Mx, uint My, const ref<byte>& title){
    real max=0; for(uint i: range(Mx*My)) max=::max(max, sqrt(X[i]*X[i]+Y[i]*Y[i]));
    subplot(position, size, count, index, vecToImage(X, Y, Mx, My, max), title, max);
}

struct CFD : Widget {
    static constexpr uint R = 128; // Resolution
    static constexpr uint Mx=R+1, My=R+1, N = Mx*My; // Mesh vertex count
    const real dx = 1./Mx, dy = 1./My; // Spatial resolution

    const real deltaT = 1; // K Temperature difference ΔT

    const real beta = 1./300; //K-1 Thermal expansion (ideal gas=1/T)
    const real eta = 2e-5; //Pa.s=kg/m/s² Dynamic viscosity
    const real rho = 1.2; //kg/m³ Density
    const real alpha = 2e-5; //m²/s Thermal diffusivity

    const real vu = eta/rho; //m²/s Kinematic viscosity
    const real g = 9.8; //m/s² Gravitational acceleration
    const real L = 0.1; //m Box side length
    const real t = L*L/alpha; //s Time unit

    const real Pr = vu/alpha; // Prandtl (ν/α)
    const real Ra = deltaT*beta*g*L*L*L/(vu*alpha); // Rayleigh ΔTβ·gL³/(να)
    const real dt = 1./(R*sqrt(Ra)); // Temporal resolution

    Matrix I = identity(N);
    Matrix P{N}, PD{N}, PDX{N}, PDY{N}; // Elementary matrices (interior points projector, Laplacian Δ and partial derivatives ∂x, ∂y)

    UMFPACK Lt, Lw, Lj, LT; // Factorized left-hand operator (implicit)
    Vector Gt{N}, GTx{N}, GTy{N}; // Right-hand vectors (BC and source field)
    Matrix Rt{N}, Rw{N}, Rj{N}, RT{N}; // Right-hand operator (explicit)
    Matrix BCt{N}, BCw{N}, BCT{N}; // Boundary conditions

    Vector Ct{N}; // Current temperature T field
    Vector Cw{N}; // Current vorticity ω field (ω=∇×u)
    Vector Pj{N}, Cj{N}; // Previous and current stream function ϕ field (u=∇×ϕ)
    Vector CTx{N}, CTy{N}; // Current texture positions Tx, Ty fields

    Vector PAt{N}, CAt{N}; // Previous and current non-linear advection term for temperature T
    Vector PAw{N}, CAw{N}; // Previous and current non-linear advection term for vorticity ω
    Vector PATx{N}, CATx{N}; // Previous and current non-linear advection term for texture positions Tx
    Vector PATy{N}, CATy{N}; // Previous and current non-linear advection term for texture positions Ty

    //Window window {this, int2(1024,1024+2*16), "CFD"_};
    Window window {this, int2(512,512+2*16), "CFD"_}; //for recording
#if RECORD
    Record record {window.size.x, window.size.y, min(30, int(1/(dt*t))) };
#endif
    CFD() {
        log(Pr,Ra,1/dt,1/(dt*t));
        window.clearBackground = false;
        window.localShortcut(Escape).connect(&exit);

        // Elementary matrices (interior points projector, Laplacian Δ and partial derivatives ∂x, ∂y)
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
                    PDX(i,i+1) = 1/(2*dx);

                    PDY(i,i-Mx) = -1/(2*dy);
                    PDY(i,i+Mx) = 1/(2*dy);
                }
            }
        }

        // Boundary condition for temperature T
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
            Gt[i] = 0;
            // Right
            BCt(i+Mx-1,i+Mx-1) = 1;
            Gt[i+Mx-1] = 1;
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

        // Constant boundary condition for texture advection
        for(uint x: range(1,Mx-1)) { // Horizontal boundaries
            uint i = x;
            // Top
            BCT(i+0*Mx,i+0*Mx) = 1;
            GTx[i+0*Mx] = real(x)/Mx, GTy[i+0*Mx] = real(0)/My;
            // Bottom
            BCT(i+(My-1)*Mx,i+(My-1)*Mx) = 1;
            GTx[i+(My-1)*Mx] = real(x)/Mx, GTy[i+(My-1)*Mx] = real(My-1)/My;
        }
        for(uint y: range(My)) { // Vertical boundaries
            uint i = y*Mx;
            // Left
            BCT(i+0,i+0) = 1;
            GTx[i+0] = real(0)/Mx, GTy[i+0] = real(y)/My;
            // Right
            BCT(i+Mx-1,i+Mx-1) = 1;
            GTx[i+Mx-1] = real(Mx-1)/Mx, GTy[i+Mx-1] = real(y)/My;
        }

        // Left-hand side (implicit) of temperature T, vorticity ω, stream function ϕ and texture advection T evolution equations
        Matrix Lt = BCt + P - (dt/2)*PD;
        Matrix Lw = I - (Pr*dt/2)*PD;
        Matrix Lj = (I-P) + PD;
        Matrix LT = BCT + P - (1./R*dt/2)*PD;

        // LU factorizations of the implicit operators
        this->Lt = UMFPACK(Lt);
        this->Lw = UMFPACK(Lw);
        this->Lj = UMFPACK(Lj);
        this->LT = UMFPACK(LT);

        // Right-hand side (explicit)
        Rt = P + (dt/2)*PD;
        Rw = P + (Pr*dt/2)*PD;
        Rj = -1*P;
        RT = P + (1./R*dt/2)*PD;

        // Sets initial temperature to a linear gradient (no transient state)
        //for(uint x: range(Mx)) for(uint y: range(My)) Ct[y*Mx+x] = real(x)/Mx;
        // Sets initial positions for texture advection
        for(uint x: range(Mx)) for(uint y: range(My)) CTx[y*Mx+x] = real(x)/Mx, CTy[y*Mx+x] = real(y)/My;
#if RECORD
        record.start("Pr="_+itoa(Pr)+",Ra="_+itoa(Ra)+",dt="_+itoa(1/(dt*t)));
#endif
    }

    Stopwatch total, update, view;
    void render(int2 position, int2 size) {
        update.start();
        // Solves evolution equations
        Vector Nt = Lt .solve(Rt*Ct   + (3*dt/2)*CAt  - (dt/2)*PAt  + Gt);
        Vector Nw=Lw.solve(Rw*Cw + (3*dt/2)*CAw - (dt/2)*PAw + (Ra*Pr*dt/2)*(PDX*(Ct+Nt)) + BCw*(2*Cj-Pj));
        Vector Nj = Lj .solve(Rj*Nw);
        Vector NTx  = LT .solve(RT*CTx   + (3*dt/2)*CATx  - (dt/2)*PATx  + GTx);
        Vector NTy  = LT .solve(RT*CTy   + (3*dt/2)*CATy  - (dt/2)*PATy  + GTy);

        // Update references
        Cw=move(Nw), PAw=move(CAw);
        Pj=move(Cj), Cj=move(Nj);
        Ct=move(Nt), PAt=move(CAt);
        CTx=move(NTx), PATx=move(CATx);
        CTy=move(NTy), PATy=move(CATy);

        // Computes advection for next step
        Vector Ux = PDY*Cj, Uy=-1*PDX*Cj;
        CAw = Ux*(PDX*Cw)+Uy*(PDY*Cw);
        CAt  = Ux*(PDX*Ct )+Uy*(PDY*Ct);
        CATx  = Ux*(PDX*CTx)+Uy*(PDY*CTx);
        CATy  = Ux*(PDX*CTy)+Uy*(PDY*CTy);
        update.stop();

        view.start();
        //Shows all fields while in transition state
        subplot(position, size, 4, 0, Cw, Mx, My, "Vorticity ω"_);
        subplot(position, size, 4, 1, Ux, Uy, Mx, My, "Velocity u"_);
        subplot(position, size, 4, 2, positiveToImage(Ct, Mx, My), "Temperature T"_);
        subplot(position, size, 4, 3, toImage(CTx, CTy, Mx, My), "Advection"_);
        view.stop();

        static uint frameCount=0; frameCount++;
#if RECORD
        if(frameCount%3) record.captureVideoFrame(); //~realtime
#endif
        if(frameCount%256==0) {
            extern Stopwatch solve;
            log("#"_+str(frameCount), "t =",frameCount*dt,"=",int(frameCount*dt*t),"s\t", //Time
                total/1000./frameCount,"ms", frameCount/(total/1000/1000.),"fps", (frameCount*dt*t)/(total/1000000.),"x", //Performance
                "update:",100.*update/total, "(solve:",100.*solve/update, "), view:",100.*view/total); //Profile
            writeFile(itoa(frameCount*dt*t)+".png"_,encodePNG(framebuffer),home());
        }
        if(frameCount==2048) { log("Stop"); return; }
        window.render();
    }
} cfd;
