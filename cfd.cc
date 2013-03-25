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

/// Converts values to colors for visualization;
template<Type T> notrace byte4 toColor(T value);
template<> byte4 toColor(real x) {
    uint8 c  = sRGB[ clip<int>(0, round(0xFF*abs(x)), 0xFF) ]; // linear to gamma (sRGB)
    return x>0 ? byte4(0,0,c,0xFF) : byte4(c,0,0,0xFF); //[-max,0,max] -> [blue,black,red]
}

#if norm_angle // Velocity vector visualization
vec3 HSVtoRGB(real h, real s, real v) {
    real H = h/PI*3, C = v*s, X = C*(1-abs(mod(H,2)-1));
    int i=H;
    if(i==0) return vec3(C,X,0);
    if(i==1) return vec3(X,C,0);
    if(i==2) return vec3(0,C,X);
    if(i==3) return vec3(0,X,C);
    if(i==4) return vec3(X,0,C);
    if(i==5) return vec3(C,0,X);
    return vec3(0,0,0);
}

template<> inline byte4 toColor(Vec2 z) {
    vec3 rgb = HSVtoRGB(atan(z.y, z.x)+PI, 1, norm(z));
    uint8 r = sRGB[ clip<int>(0, round(0xFF*rgb.x), 0xFF) ]; // linear to gamma (sRGB)
    uint8 g = sRGB[ clip<int>(0, round(0xFF*rgb.y), 0xFF) ]; // linear to gamma (sRGB)
    uint8 b = sRGB[ clip<int>(0, round(0xFF*rgb.z), 0xFF) ]; // linear to gamma (sRGB)
    return byte4(b,g,r,0xFF);
}
#endif

template<> byte4 toColor(Vec2 z) {
#if 1
    uint8 r = sRGB[ clip<int>(0, round(0xFF*z.x), 0xFF) ]; // linear to gamma (sRGB)
    uint8 g = sRGB[ clip<int>(0, round(0xFF*z.y), 0xFF) ]; // linear to gamma (sRGB)
#else
    // Grid
    uint8 r = sRGB[ int(round(z.x*0x1000))%0x100 ]; // linear to gamma (sRGB)
    uint8 g = sRGB[ int(round(z.y*0x1000))%0x100 ]; // linear to gamma (sRGB)
#endif
    return byte4(0,g,r,0xFF);
}

struct CFDTest : Widget {
    static constexpr uint R = 256; // Resolution
    static constexpr uint Mx=R+1, My=R+1, N = Mx*My; // Mesh vertex count
    const real H = 1; // Aspect ratio (y/x)
    const real dx = 1./Mx, dy = H/My; // Spatial resolution
    const real Ra = 1; // Rayleigh
    const real Pr = 1; // Prandtl
    real dt; // Temporal resolution

    Matrix I = identity(N);
    Matrix P{N}, PD{N}, PDX{N}, PDY{N}; // Elementary matrices (interior points projector, Laplacian Δ and partial derivatives ∂x, ∂y)

    UMFPACK Lw, Lj, Lt, LT; // Factorized left-hand operator (implicit)
    Matrix Rw{N}, Rj{N}, Rt{N}, RT{N}; // Right-hand operator (explicit)
    Matrix BCw{N}, BCt{N}, BCT{N}; // Boundary conditions
    Vector Gt{N}, GTx{N}, GTy{N}; // Right-hand vectors (BC and source field)

    Vector Pj{N}, Cj{N}; // Previous and current stream function ϕ field
    Vector Cw{N}; // Current vorticity ω field
    Vector Ct{N}; // Previous temperature T field
    Vector CTx{N}, CTy{N}; // Current texture positions Tx, Ty fields

    Vector PNLw{N}, CNLw{N}; // Previous and current non-linear advection term for vorticity ω
    Vector PNLt{N}, CNLt{N}; // Previous and current non-linear advection term for temperature T
    Vector PNLTx{N}, CNLTx{N}; // Previous and current non-linear advection term for texture positions Tx
    Vector PNLTy{N}, CNLTy{N}; // Previous and current non-linear advection term for texture positions Ty

    Window window {this, int2(1024,1024+2*16), "CFDTest"_};
    CFDTest() {
        window.clearBackground = false;
        window.localShortcut(Escape).connect(&exit);

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
            Gt[i] = 1;
            // Right
            BCt(i+Mx-1,i+Mx-1) = 1;
            Gt[i+Mx-1] = 0;
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
        setTimeStep(1./(4*R*R)); //FIXME: R²
    }
    void setTimeStep(real dt) {
        this->dt = dt;
        // Left-hand side (implicit) of ω,ϕ,T evolution equations
        Matrix Lw = I - (Pr*dt/2)*PD;
        Matrix Lj = (I-P) + PD;
        Matrix Lt = BCt + P - (dt/2)*PD;
        Matrix LT = BCT + P - (0.*dt/2)*PD;

        // LU factorizations of the implicit operators
        this->Lw = UMFPACK(Lw);
        this->Lj = UMFPACK(Lj);
        this->Lt = UMFPACK(Lt);
        this->LT = UMFPACK(LT);

        // Right-hand side (explicit)
        Rw = P + (Pr*dt/2)*PD;
        Rj = -1*P;
        Rt = P + (dt/2)*PD;
        RT = P + (0.*dt/2)*PD;

        // Sets initial temperature to a linear gradient (no transient state)
        for(uint x: range(Mx)) for(uint y: range(My)) Ct[y*Mx+x] = 1-real(x)/Mx;
        // Sets initial positions for texture advection
        for(uint x: range(Mx)) for(uint y: range(My)) CTx[y*Mx+x] = real(x)/Mx, CTy[y*Mx+x] = real(y)/My;
    }

    template<uint sqrt, Type T> void subplot(int2 position, int2 size, uint index, const buffer<T>& field, const ref<byte>& title) {
        int2 subSize = size/int(sqrt);
        int2 subPosition = position+int2(index%sqrt,index/sqrt)*subSize;
        real max=0; for(T v: field) max=::max(max, norm(v));
        fill(subPosition+Rect(subSize.x,16),black);
        Text(title+"["_+ftoa(max,1,0,true)+"]"_,16,white).render(subPosition,int2(subSize.x,16));
        Image image = clip(framebuffer, subPosition+int2(0,16), subSize-int2(0,16));
        constexpr uint scale = 1024/R/sqrt;
        for(uint y: range(My-1)) {
            for(uint x: range(Mx-1)) {
#if 1
                T v00 = field[(y+0)*Mx+(x+0)]/max;
                T v01 = field[(y+0)*Mx+(x+1)]/max;
                T v10 = field[(y+1)*Mx+(x+0)]/max;
                T v11 = field[(y+1)*Mx+(x+1)]/max;
                byte4* data = image.data+y*scale*image.stride+x*scale;
                for(uint dy: range(scale)) {
                    for(uint dx: range(scale)) {
                        real u=real(dx)/scale, v=real(dy)/scale;
                        data[dy*image.stride+dx] = toColor( ( v00 * (1-u) + v01 * u) * (1-v) + ( v10 * (1-u) + v11 * u) * v );
                    }
                }
#else
                byte4 nearest = toColor( field[(y+0)*Mx+(x+0)]/max );
                byte4* data = image.data+y*scale*image.stride+x*scale;
                for(uint dy: range(scale)) for(uint dx: range(scale)) data[dy*image.stride+dx] = nearest;
#endif
            }
        }
    }

    Stopwatch total, update, solve, view;
    void render(int2 position, int2 size) {
        for(uint unused t: range(1/*256*/)) {
            // Solves evolution equations
            solve.start();
            Vector Nt = Lt .solve(Rt*Ct   + (3*dt/2)*CNLt  - (dt/2)*PNLt  + Gt);
            Vector Nw=Lw.solve(Rw*Cw + (3*dt/2)*CNLw - (dt/2)*PNLw + (Ra*Pr*dt/2)*(PDX*(Nt+Ct)) + BCw*(2*Cj-Pj));
            Vector Nj = Lj .solve(Rj*Nw);
#if 1
            Vector NTx  = LT .solve(RT*CTx   + (3*dt/2)*CNLTx  - (dt/2)*PNLTx  + GTx);
            Vector NTy  = LT .solve(RT*CTy   + (3*dt/2)*CNLTy  - (dt/2)*PNLTy  + GTy);
#else
            Vector NTx  = LT .solve(RT*CTx   + (3*1./2)*CNLTx  - (1./2)*PNLTx  + GTx);
            Vector NTy  = LT .solve(RT*CTy   + (3*1./2)*CNLTy  - (1./2)*PNLTy  + GTy);
#endif
            solve.stop();

            // Update references
            update.start();
            Cw=move(Nw), PNLw=move(CNLw);
            Pj=move(Cj), Cj=move(Nj);
            Ct=move(Nt), PNLt=move(CNLt);
            CTx=move(NTx), PNLTx=move(CNLTx);
            CTy=move(NTy), PNLTy=move(CNLTy);

            // Computes advection for next step
            Vector Ux = PDY*Cj, Uy=-1*PDX*Cj;
            CNLw = Ux*(PDX*Cw)+Uy*(PDY*Cw);
            CNLt  = Ux*(PDX*Ct )+Uy*(PDY*Ct);
            CNLTx  = Ux*(PDX*CTx)+Uy*(PDY*CTx);
            CNLTy  = Ux*(PDX*CTy)+Uy*(PDY*CTy);
            update.stop();
        }

        view.start();
        subplot<2>(position, size, 0, Cw,"Vorticity ω"_);
        subplot<2>(position, size, 1, Cj,"Stream function ϕ"_);
        subplot<2>(position, size, 2, Ct,"Temperature T"_);
        // Velocity visualization
        //buffer<Vec2> U(N); for(uint i: range(N)) U[i]=Vec2(Ux[i],Uy[i]);
        //subplot<2>(position, size, 3, U,"Velocity u"_);
        // Advection visualization
        buffer<Vec2> T(N); for(uint i: range(N)) T[i]=Vec2(CTx[i],CTy[i]);
        subplot<2>(position, size, 3, T,"Advection"_);
        //TODO: stream lines
        view.stop();

        extern Stopwatch umfpack;
        static uint frameCount=0; frameCount++;
        /*if(frameCount==256) { //FIXME: transition shouldn't require such a small timestep in the first place
            log("setTimeStep",1./(R*R)); setTimeStep(1./(R*R)); log("Done");
        }*/
        if(frameCount%256==0)
            log(frameCount, total/1000/frameCount,"ms/frame", frameCount/(total/1000/1000.),"fps",
                "update",100.*update/total,
                "solve",100.*solve/total, "UMF",100.*umfpack/solve,
                "view",100.*view/total,
                "misc"_,100.*int64(total-solve-update-view)/total);
            //log(view/frameCount/1000.,"ms");
        if(frameCount ==8192) { log("Stop"); return; }
        window.render();
    }
} test;
