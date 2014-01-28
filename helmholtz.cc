#include "thread.h"
#include "algebra.h"
#include "math.h"
#include "lu.h"
#include "time.h"
#include "window.h"
#include "interface.h"

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

// Resolution (dt - L)T = S
struct Helmholtz {
    const real L = 1;
    const real kx = 2*PI/L, ky = 2*PI/L;
    real Ta(real x, real y) { return sin(kx*x) * cos(ky*y); }
    real LTa(real x, real y) { return - (sq(kx) + sq(ky)) * Ta(x,y); }
    real DxTa(real x, real y) { return + kx * cos(kx*x) * cos(ky*y); }
    real DyTa(real x, real y) { return - ky * sin(kx*x) * sin(ky*y); }
    const real dt = 1;

    Helmholtz() {
        Time total;
        for(uint N: range(4,24 +1)) {
            const uint Nx=N, Ny=Nx;
            // Maillage
            Vector X (Nx), Y (Ny);
#if 0 // Irregulier
            for(uint i: range(Nx)) X[i] = L/2 * (1 - cos(i*PI/(Nx-1)));
            for(uint j: range(Ny)) Y[j] = L/2 * (1 - cos(j*PI/(Ny-1)));
#else
            for(uint i: range(Nx)) X[i] = L * i / (Nx-1);
            for(uint j: range(Ny)) Y[j] = L * j / (Ny-1);
#endif

            // Solution analytique
            Vector2D T (Nx,Ny);
            for(uint i: range(Nx)) for(uint j: range(Ny)) T(i,j) = Ta(X[i], Y[j]);

            // Champ solution
            Vector2D u (Nx,Ny);
            u.clear(0); // Condition initial

            // Systeme MT=S
            Matrix2D M (Nx, Ny); // Operateur [256^2]
            Vector2D S (Nx,Ny); // Constante
            M.elements.clear(0);
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
                M(i,j, i-1,j)   = left;
                M(i,j, i+1,j)   =       right;
                M(i,j, i,  j-1) =             top;
                M(i,j, i,  j+1) =                 bottom;
                M(i,j, i,  j  ) = -left-right-top-bottom+H;
            }
#if 0 // Dirichlet
            for(uint i: range(Nx)) {
                M(i, 0,    i, 0)    = 1, S(i, 0   ) = T(i, 0);
                M(i, Ny-1, i, Ny-1) = 1, S(i, Ny-1) = T(i, Ny-1);
            }
            for(uint j: range(Ny)) {
                M(0,   j, 0,   j) = 1, S(0, j)    = T(0, j);
                M(Nx-1,j, Nx-1,j) = 1, S(Nx-1, j) = T(Nx-1, j);
            }
#else // Neumann
            auto bord = [&](bool transpose, int i0, int i1, int i, real f) {
                const auto& x = !transpose ? X : Y;
                const auto& y = !transpose ? Y : X;
                real xm = (x[i-1]+x[i])/2;
                real xp = (x[i]+x[i+1])/2;
                real y12 = (y[i0]+y[i1])/2;
                real left  = (y12-y[i0])/(x[i]-x[i-1]);
                real right = (y12-y[i0])/(x[i+1]-x[i]);
                real bottom = (xp-xm)/(y[i1]-y[i0]);
                auto m = [&](int i, int j, int k, int l) -> real& { return !transpose ? M(i,j,k,l) : M(j,i,l,k); };
                m(i,i0, i-1,i0) =  left;
                m(i,i0, i  ,i0) = -left-right-bottom;
                m(i,i0, i+1,i0) =       right;
                m(i,i0, i,  i1) =             bottom;
                auto lta = [&](int i, int j) -> real { return !transpose ? LTa(X[i],Y[j]) : LTa(X[j],Y[i]); };
                real b = lta(i, i0);
                auto s = [&](int i, int j) -> real& { return !transpose ? S(i,j) : S(j,i); };
                s(i,i0) = (xp-xm) * ((y12-y[i0]) * b + f);
            };
            for(uint i: range(1,Nx-1)) {
                bord(false, 0,    1,      i, + DyTa(X[i], Y[0   ]));
                bord(false, Ny-1, Ny-1-1, i, + DyTa(X[i], Y[Ny-1]));
            }
            for(uint j: range(1,Ny-1)) {
                bord(true,  0,    1,      j, + DxTa(X[0   ], Y[j]));
                bord(true,  Nx-1, Nx-1-1, j, + DxTa(X[Nx-1], Y[j]));
            }
            // Dirichlet au coins
            M(0, 0,    0, 0)          = 1, S(0,    0)    = T(0,    0);
            M(Nx-1, 0,    Nx-1, 0)    = 1, S(Nx-1, 0)    = T(Nx-1, 0);
            M(0,    Ny-1, 0, Ny-1)    = 1, S(0, Ny-1)    = T(0,    Ny-1);
            M(Nx-1, Ny-1, Nx-1, Ny-1) = 1, S(Nx-1, Ny-1) = T(Nx-1, Ny-1);
#endif
            log_("N="_+str(Nx)+"x"_+str(Ny)+" "_);
            Time time;
            PLU plu = factorize(move(M));
            log("T="_+str(time));
            real e = inf;
            for(uint t=0;; t++) {
                for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) { // Source interieur
                    real xm = (X[i-1]+X[i])/2;
                    real xp = (X[i]+X[i+1])/2;
                    real ym = (Y[j-1]+Y[j])/2;
                    real yp = (Y[j]+Y[j+1])/2;
                    real H = (xp-xm)*(yp-ym)/dt;
                    S(i,j) = H*u(i,j) + (xp-xm)*(yp-ym) * LTa(X[i],Y[j]);
                }
                u = Vector2D(solve(plu, S), Nx, Ny);
                real eMax = maxabs(u-T);
                log(t, "e="_+ftoa(eMax*100,1)+"%"_);
                if(eMax >= e) break;
                e = eMax;
            }
        }
        log(total);
    }
} test;
