#include "thread.h"
#include "algebra.h"
#include "math.h"
#include "time.h"
#include "window.h"
#include "interface.h"
#include "plot.h"
#include "encoder.h"

real maxabs(const ref<real>& v) { real y=0; for(real x: v) if(abs(x)>y) y=abs(x); return y; }

// 2D field as a vector
struct Vector2D : Vector {
    uint m, n;
    Vector2D():m(0),n(0){}
    Vector2D(Vector&& v, uint m, uint n):Vector(move(v)),m(m),n(n){}
    Vector2D(uint m, uint n):Vector(m*n),m(m),n(n){}
    real& operator ()(uint i, uint j) { return at(j*m+i); }
    const real& operator ()(uint i, uint j) const { return at(j*m+i); }
};
inline Vector2D operator-(const Vector2D& a, const Vector2D& b) { assert_(a.m==b.m && a.n==b.n); return Vector2D((const vector&)a-b,a.m,a.n); }
inline String str(const Vector2D& v) {
    String s;
    for(uint i: range(v.m)) {
        for(uint j: range(v.n)) {
            s<<ftoa(v(i,j),2,6);
        }
        if(i<v.m-1) s<<'\n';
    }
    return s;
}

// 2D field matrix (4D tensor) as a matrix
struct Matrix2D : Matrix {
    const uint m, n;
    Matrix2D(uint m, uint n):Matrix(m*n),m(m),n(n){}
    real operator ()(uint i, uint j, uint k, uint l) const { return Matrix::operator ()(j*m+i, l*m+k); }
    real& operator ()(uint i, uint j, uint k, uint l) { return Matrix::operator ()(j*m+i, l*m+k); }
};
inline String str(const Matrix2D& M) {
    String s;
    s << repeat("-"_,M.m*(M.n*5+3)) << '\n';
    for(uint i: range(M.m)) {
        for(uint j: range(M.n)) {
            for(uint k: range(M.m)) {
                for(uint l: range(M.n)) {
                    s<<ftoa(M(i,j,k,l),1,5);
                }
                s<<" | "_;
            }
            if(i<M.m-1 || j<M.m-1) s<<'\n';
        }
        s << repeat("-"_,M.m*(M.n*5+3)) << '\n';
    }
    return s;
}

inline String str(const Matrix2D& M, const Vector2D& v) {
    String s;
    s << repeat("-"_,M.m*(M.n*5+2)) << '\n';
    for(uint i: range(M.m)) {
        for(uint j: range(M.n)) {
            for(uint k: range(M.m)) {
                for(uint l: range(M.n)) {
                    s << ftoa(M(i,j,k,l),1,5);
                }
                s<<" |"_;
            }
            s << ftoa(v(i,j),1,5);
            s <<'\n';
        }
        s << repeat("-"_,M.m*(M.n*5+2));
        if(i<M.m-1) s << '\n';
    }
    return s;
}

// Resolution (1/dt - L)T = T0/dt - S
struct Helmholtz : Widget {
    const real L = 1;
    const real kx = 2*PI/L, ky = 2*PI/L;
    real Ta(real x, real y) { return sin(kx*x) * cos(ky*y); }
    real LTa(real x, real y) { return - (sq(kx) + sq(ky)) * Ta(x,y); }
    real DxTa(real x, real y) { return + kx * cos(kx*x) * cos(ky*y); }
    real DyTa(real x, real y) { return - ky * sin(kx*x) * sin(ky*y); }
    const bool regularGrid; // Spatial discretisation
    const bool dirichletBoundaryCondition; // Boundary condition
    const real dt; // Time step
    Helmholtz(bool regularGrid, bool dirichletBoundaryCondition, const real dt = inf) :
        regularGrid(regularGrid), dirichletBoundaryCondition(dirichletBoundaryCondition), dt(dt) {}

    Vector X, Y;
    Vector2D e;
    real eMax;

    real solve(uint N) {
        // Maillage
        const uint Nx=N, Ny=Nx;
        X = Vector(Nx), Y = Vector(Ny);
        if(regularGrid) {
            for(uint i: range(Nx)) X[i] = L * i / (Nx-1);
            for(uint j: range(Ny)) Y[j] = L * j / (Ny-1);
        } else { // Refine near boundaries
            for(uint i: range(Nx)) X[i] = L/2 * (1 - cos(i*PI/(Nx-1)));
            for(uint j: range(Ny)) Y[j] = L/2 * (1 - cos(j*PI/(Ny-1)));
        }

        // Solution analytique
        Vector2D T (Nx,Ny);
        for(uint i: range(Nx)) for(uint j: range(Ny)) T(i,j) = Ta(X[i], Y[j]);

        // Champ solution
        Vector2D u (Nx,Ny);
        u.clear(0); // Condition initial

        // Systeme MT=S
        Matrix2D M (Nx, Ny); // Operateur [256^2]
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real xm = (X[i-1]+X[i])/2;
            real xp = (X[i]+X[i+1])/2;
            real ym = (Y[j-1]+Y[j])/2;
            real yp = (Y[j]+Y[j+1])/2;
            real left = (yp-ym)/(X[i]-X[i-1]);
            real right = (yp-ym)/(X[i+1]-X[i]);
            real top = (xp-xm)/(Y[j]-Y[j-1]);
            real bottom = (xp-xm)/(Y[j+1]-Y[j]);
            real H = (xp-xm)*(yp-ym)/dt;
            M(i,j, i-1,j)   =    -left;
            M(i,j, i+1,j)   =         -right;
            M(i,j, i,  j-1) =                     -top;
            M(i,j, i,  j+1) =                           -bottom;
            M(i,j, i,  j  ) = H+left+right+top+bottom;
        }
        Vector2D S (Nx,Ny); // Constante
        if(dirichletBoundaryCondition) {
            for(uint i: range(Nx)) {
                M(i, 0,    i, 0)    = 1, S(i, 0   ) = T(i, 0);
                M(i, Ny-1, i, Ny-1) = 1, S(i, Ny-1) = T(i, Ny-1);
            }
            for(uint j: range(Ny)) {
                M(0,   j, 0,   j) = 1, S(0, j)    = T(0, j);
                M(Nx-1,j, Nx-1,j) = 1, S(Nx-1, j) = T(Nx-1, j);
            }
        } else {
            auto bord = [&](bool transpose, int i0, int i1, int i) {
                const auto& x = !transpose ? X : Y;
                const auto& y = !transpose ? Y : X;
                real xm = (x[i-1]+x[i])/2;
                real xp = (x[i]+x[i+1])/2;
                real y12 = (y[i0]+y[i1])/2;
                real left  = (y12-y[i0])/(x[i]-x[i-1]);
                real right = (y12-y[i0])/(x[i+1]-x[i]);
                real bottom = (xp-xm)/(y[i1]-y[i0]);
                real H = (xp-xm)*(y12-y[i0])/dt;
                auto m = [&](int i, int j, int k, int l) -> real& { return !transpose ? M(i,j,k,l) : M(j,i,l,k); };
                m(i,i0, i-1,i0) =  -left; // -Laplacian
                m(i,i0, i+1,i0) =       -right; // -Laplacian
                m(i,i0, i,  i1) =                   -bottom; // Flux //FIXME: account for laplacian?
                m(i,i0, i  ,i0) = H+left+right+bottom;
            };
            for(uint i: range(1,Nx-1)) {
                bord(false, 0,    1,      i);
                bord(false, Ny-1, Ny-1-1, i);
            }
            for(uint j: range(1,Ny-1)) {
                bord(true,  0,    1,      j);
                bord(true,  Nx-1, Nx-1-1, j);
            }
            // Dirichlet au coins
            M(0, 0,    0, 0)          = 1, S(0,    0)    = T(0,    0);
            M(Nx-1, 0,    Nx-1, 0)    = 1, S(Nx-1, 0)    = T(Nx-1, 0);
            M(0,    Ny-1, 0, Ny-1)    = 1, S(0, Ny-1)    = T(0,    Ny-1);
            M(Nx-1, Ny-1, Nx-1, Ny-1) = 1, S(Nx-1, Ny-1) = T(Nx-1, Ny-1);
        }
        log_((regularGrid ? "Regular"_:"Irregular"_) + " - "_ + (dirichletBoundaryCondition ? "Dirichlet"_:"Neumann"_)+" N="_+str(Nx)+"x"_+str(Ny)+" "_);
        Time time;
        UMFPACK A = M;
        log("T="_+str(time));
        eMax = inf;
        for(uint t: range(4/dt +1)) {
            for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) { // Source interieur
                real xm = (X[i-1]+X[i])/2;
                real xp = (X[i]+X[i+1])/2;
                real ym = (Y[j-1]+Y[j])/2;
                real yp = (Y[j]+Y[j+1])/2;
                S(i,j) = (xp-xm)*(yp-ym) * (u(i,j)/dt - LTa(X[i],Y[j]));
            }
            if(!dirichletBoundaryCondition) {
                auto bord = [&](bool transpose, int i0, int i1, int i) {
                    const auto& x = !transpose ? X : Y;
                    const auto& y = !transpose ? Y : X;
                    real xm = (x[i-1]+x[i])/2;
                    real xp = (x[i]+x[i+1])/2;
                    real y12 = (y[i0]+y[i1])/2;
                    auto source = [&](int i, int j) -> real { return !transpose ? u(i,j)/dt - LTa(X[i],Y[j]) : u(j,i)/dt - LTa(X[j],Y[i]); };
                    real b = source(i, i0);
                    auto s = [&](int i, int j) -> real& { return !transpose ? S(i,j) : S(j,i); };
                    real f = !transpose ? DyTa(X[i], Y[i0]) : DxTa(X[i0], Y[i]);
                    s(i,i0) = (xp-xm) * ((y12-y[i0]) * b - f);
                };
                for(uint i: range(1,Nx-1)) {
                    bord(false, 0,    1,      i);
                    bord(false, Ny-1, Ny-1-1, i);
                }
                for(uint j: range(1,Ny-1)) {
                    bord(true,  0,    1,      j);
                    bord(true,  Nx-1, Nx-1-1, j);
                }
            }
            u = Vector2D(A.solve(S), Nx, Ny);
            e = u-T;
            real eMaxt = maxabs(e);
            log(t, "e="_+ftoa(eMaxt*100,2)+"%"_);
            //if(eMaxt >= eMax) break;
            eMax = eMaxt;
        }
        return eMax;
    }
    int2 sizeHint() { return int2(720/2); }
    void render(int2 position, int2 size) override {
        for(uint i: range(e.m-1)) for(uint j: range(e.n-1)) {
            for(uint y: range(round(size.y*Y[j]),round(size.y*Y[j+1]))) {
                for(uint x: range(round(size.x*X[i]),round(size.x*X[i+1]))) {
                    real u = ((x+1./2)/size.x-X[i])/(X[i+1]-X[i]);
                    real v = ((y+1./2)/size.y-Y[j])/(Y[j+1]-Y[j]);
                    real s =
                            (1-v) * ((1-u) * e(i,j  ) + u * e(i+1,j  )) +
                            v     * ((1-u) * e(i,j+1) + u * e(i+1,j+1));
                    uint8 intensity = clip<real>(0,round((1+s/eMax)/2*0xFF),0xFF);
                    framebuffer(position.x + x, position.y + y) = byte4(intensity, intensity, intensity, 0xFF);
                }
            }
        }
    }
};

struct Application {
    UniformGrid<Helmholtz> problems {{Helmholtz(false, false), Helmholtz(false, true), Helmholtz(true, false), Helmholtz(true, true)}};
    Plot plot { apply(problems, [](const Helmholtz& problem){ return
                (problem.regularGrid ? "Regular"_:"Irregular"_) + " - "_ +
                (problem.dirichletBoundaryCondition ? "Dirichlet"_:"Neumann"_); }) };
    HBox layout {{&plot, &problems}};

    /// Solves Helmholtz problems at resolution NxN
    void solve(uint N) {
        real maxE = 0;
        for(uint i: range(problems.size)) {
            real e = problems[i].solve(N);
            plot.dataSets[i].insertMulti(N, e);
            maxE = max(maxE, e);
        }
        for(Helmholtz& problem: problems) problem.eMax = maxE; // Normalizes all problems display with the same maximum error
    }

#if 1
    uint N = 8;
    Window window {&layout, int2(1280,720), str(N)};
    Application() {
        solve(N);
        window.backgroundColor = window.backgroundCenter = 1;
        window.localShortcut(Escape).connect([]{exit();});
        window.frameSent.connect([this](){
            N++;
            solve(N);
            window.render();
            window.setTitle(str(N));
        });
        window.show();
#else
    Application() {
        Encoder encoder {"Helmholtz"_};
        for(uint N: range(8, 64 +1)) {
            solve(N);
            encoder.writeVideoFrame(renderToImage(layout, encoder.size()));
        }
#endif
    }
} app;
