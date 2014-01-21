#include "thread.h"
#include "algebra.h"
#include "math.h"
#include "lu.h"
#include "window.h"
#include "interface.h"

// 2D field as a vector
struct Vector2D : Vector {
    const uint m, n;
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
inline String str(const Matrix2D& M) { return str((const Matrix&)M); }
/*inline String str(const Matrix2D& M) {
    String s;
    for(uint i: range(M.m)) {
        for(uint k: range(M.m)) {
            for(uint j: range(M.n)) {
                for(uint l: range(M.n)) {
                    s<<ftoa(M(i,j,k,l),2,6);
                }
            }
            if(i<M.m-1) s<<'\n';
        }
    }
    return s;
}*/

// Resolution MT=S
struct Poisson {
    Poisson() {
        const uint Nx=4, Ny=Nx;
        const real L = 1;
        // Maillage (irregulier)
        Vector X (Nx), Y (Ny);
        //for(uint i: range(Nx)) X[i] = L/2 * (1 - cos(i*PI/(Nx-1)));
        for(uint i: range(Nx)) X[i] = L * i / (Nx-1);
        for(uint j: range(Ny)) Y[j] = L * j / (Ny-1);
        // Solution analytique
        Vector2D T (Nx,Ny);
        for(uint i: range(Nx)) for(uint j: range(Ny)) T(i,j) = sin(2 * PI/L * X[i]) * cos(2 * PI/L * Y[j]);

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
            M(i,j, i-1,j)   = left;
            M(i,j, i+1,j)   =       right;
            M(i,j, i,  j-1) =             top;
            M(i,j, i,  j+1) =                 bottom;
            M(i,j, i,  j  ) = -left-right-top-bottom;
            S(i,j) = (xp-xm)*(yp-ym) * -2 * sq(2*PI/L) * T(i,j);
        }
#if DIRICHLET
        for(uint i: range(Nx)) {
            M(i, 0,    i, 0)    = 1, S(i, 0   ) = T(i, 0);
            M(i, Ny-1, i, Ny-1) = 1, S(i, Ny-1) = T(i, Ny-1);
        }
        for(uint j: range(Ny)) {
            M(0,   j, 0,   j) = 1, S(0, j)    = T(0, j);
            M(Nx-1,j, Nx-1,j) = 1, S(Nx-1, j) = T(Nx-1, j);
        }
#else // Neumann
#if 0
        for(uint i: range(1,Nx-1)) {
            auto bordX = [&](int j0, int j1) {
             real y12 = (Y[j0]+Y[j1])/2;
             real left  = (y12-Y[j0])/(X[i]-X[i-1]);
             real right = (y12-Y[j0])/(X[i+1]-X[i]);
             real xm = (X[i-1]+X[i])/2;
             real xp = (X[i]+X[i+1])/2;
             real bottom = (xp-xm)/(Y[j1]-Y[j0]);
             M(i,j0, i-1,j0) =  left;
             M(i,j0, i  ,j0) = -left-right-bottom;
             M(i,j0, i+1,j0) =       right;
             M(i,j0, i+1,j1) =             bottom;
             real s = (y12-Y[j0]) * -2 * sq(2*PI/L) * T(i,j0); // Source interieur
             real f = - 2*PI/L * sin(2*PI/L*X[i]) * sin(2*PI/L*Y[j0]);  // Flux
             S(i, j0) = (xp-xm) * (s - f);
            };
            bordX(0,    1);
            bordX(Ny-1, Ny-1-1);
        }
        for(uint j: range(1,Ny-1)) {
            auto bordY = [&](int i0, int i1) {
             real x12 = (X[i0]+X[i1])/2;
             real top = (x12-X[i0])/(Y[j]-Y[j-1]);
             real bottom = (x12-X[i0])/(Y[j+1]-Y[j]);
             real ym = (Y[j-1]+Y[j])/2;
             real yp = (Y[j]+Y[j+1])/2;
             real right = (yp-ym)/(X[i1]-X[i0]);
             M(i0,j, i0,j-1) =  top;
             M(i0,j, i0,j  ) = -top-bottom-right;
             M(i0,j, i0,j+1) =      bottom;
             M(i0,j, i1,j+1) =             right;
             real s = (x12-X[i0]) * -2 * sq(2*PI/L) * T(i0,j); // Source interieur
             real f = 2*PI/L * cos(2*PI/L*X[i0]) * cos(2*PI/L*Y[j]);  // Flux
             S(i0, j) = (yp-ym) * (s - f);
            };
            bordY(0,    1);
            bordY(Nx-1, Nx-1-1);
        }
#else
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
         real b = (y12-y[i0]) * -2 * sq(2*PI/L) * T(i,i0); // Source interieur
         auto s = [&](int i, int j) -> real& { return !transpose ? S(i,j) : S(j,i); };
         s(i,i0) = (xp-xm) * (y12-y[0]) * (b - f);
        };
        for(uint i: range(1,Nx-1)) {
            bord(false, 0,    1,      i, - 2*PI/L * sin(2*PI/L*X[i]) * sin(2*PI/L*Y[0   ]));
            bord(false, Ny-1, Ny-1-1, i, + 2*PI/L * sin(2*PI/L*X[i]) * sin(2*PI/L*Y[Ny-1]));
        }
        for(uint j: range(1,Ny-1)) {
            bord(true, 0,    1,       j, + 2*PI/L * cos(2*PI/L*X[0   ]) * cos(2*PI/L*Y[j]));
            bord(true, Nx-1, Nx-1-1,  j, - 2*PI/L * cos(2*PI/L*X[Nx-1]) * cos(2*PI/L*Y[j]));
        }
#endif
        // Dirichlet au coins
        M(0, 0,    0, 0)          = 1, S(0,    0)    = T(0,    0);
        M(Nx-1, 0,    Nx-1, 0)    = 1, S(Nx-1, 0)    = T(Nx-1, 0);
        M(0,    Ny-1, 0, Ny-1)    = 1, S(0, Ny-1)    = T(0,    Ny-1);
        M(Nx-1, Ny-1, Nx-1, Ny-1) = 1, S(Nx-1, Ny-1) = T(Nx-1, Ny-1);
#endif
        log("M"); log(M);
        log("S"); log(S);

        log_("Solving..."_);
        Vector2D u (solve(M, S), Nx, Ny);
        log("Done");
        log("T"); log(T);
        log("u"); log(u);
        //log(max(u-T));
        //Vector2D e = u-T;
        const Vector2D& e = u;

        const uint size = 1024;
        Image& image = *(new Image(size,size));
        for(uint i: range(Nx-1)) for(uint j: range(Ny-1)) {
            for(uint y: range(round(size*Y[j]),round(size*Y[j+1]))) for(uint x: range(round(size*X[i]),round(size*X[i+1]))) {
                real u = ((x+1./2)/size-X[i])/(X[i+1]-X[i]);
                real v = ((y+1./2)/size-Y[j])/(Y[j+1]-Y[j]);
                real s =
                        (1-v) * ((1-u) * e(i,j  ) + u * e(i+1,j  )) +
                        v     * ((1-u) * e(i,j+1) + u * e(i+1,j+1));
#if 0
                assert_(s>=-1 && s<=1);
#else
                s = clip(-1., s, 1.);
#endif
                image(x, y) = (1+s/max(e))/2*0xFF;
            }
        }
        while(image.size() <= int2(512)) image=upsample(image);
        Window& window = *(new Window(new ImageWidget(image),int2(-1),"Poisson"));
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
    }
} test;
