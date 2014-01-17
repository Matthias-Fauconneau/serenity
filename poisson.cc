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
    const uint m;
    Matrix2D(uint m, uint n):Matrix(m*n),m(m){}
    real& operator ()(uint i, uint j, uint k, uint l) { return Matrix::operator ()(j*m+i, l*m+k); }
};
inline String str(const Matrix2D& M) { return str((const Matrix&)M); }

// Resolution MT=S
struct Poisson {
    Poisson() {
        const uint Nx=32, Ny=32;
        const real L = 1;
        // Maillage (irregulier)
        Vector X (Nx), Y (Ny);
        for(uint i: range(Nx)) X[i] = L/2 * (1 - cos(i*PI/(Nx-1)));
        for(uint j: range(Ny)) Y[j] = L/2 * (1 - cos(j*PI/(Ny-1)));

        Vector2D T (Nx,Ny);
        for(uint i: range(Nx)) for(uint j: range(Ny)) {
            T(i,j) = sin(2 * PI/L * X[i]) * cos(2 * PI/L * Y[j]);
        }

        Vector2D S (Nx,Ny);
        for(uint i: range(1,Nx-1)) for(uint j: range(1,Ny-1)) {
            real xm = (X[i-1]+X[i])/2;
            real xp = (X[i]+X[i+1])/2;
            real ym = (Y[j-1]+Y[j])/2;
            real yp = (Y[j]+Y[j+1])/2;
            S(i,j) = (xp-xm)*(yp-ym) * -2 * sq(2*PI/L) * T(i,j);
        }
        for(uint i: range(Nx)) S(i, 0) = T(i, 0), S(i, Ny-1) = T(i, Ny-1);
        for(uint j: range(Ny)) S(0, j) = T(0, j), S(Nx-1, j) = T(Nx-1, j);

        Matrix2D M (Nx, Ny); // 256^2
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
            M(i,j, i-1,j) = left;
            M(i,j, i+1,j) = right;
            M(i,j, i,j-1) = top;
            M(i,j, i,j+1) = bottom;
            M(i,j, i,j) = -left-right-top-bottom;
        }
        for(uint i: range(Nx)) M(i,0, i,0) = 1, M(i, Ny-1, i, Ny-1) = 1;
        for(uint j: range(Ny)) M(0,j, 0,j) = 1, M(Nx-1,j, Nx-1,j) = 1;
        //log("M"); log(M);

        log_("Solving..."_);
        Vector2D u (solve(M, S), Nx, Ny);
        log("Done");
        log(max(u-T));
        Vector2D e = u-T;
        //const Vector2D& e = u;

        const uint size = 1024;
        Image& image = *(new Image(size,size));
        for(uint i: range(Nx-1)) for(uint j: range(Ny-1)) {
            for(uint y: range(round(size*Y[j]),round(size*Y[j+1]))) for(uint x: range(round(size*X[i]),round(size*X[i+1]))) {
                real u = ((x+1./2)/size-X[i])/(X[i+1]-X[i]);
                real v = ((y+1./2)/size-Y[j])/(Y[j+1]-Y[j]);
                real s =
                        (1-v) * ((1-u) * e(i,j  ) + u * e(i+1,j  )) +
                        v     * ((1-u) * e(i,j+1) + u * e(i+1,j+1));
                assert_(s>=-1 && s<=1);
                image(x, y) = (1+s/max(e))/2*0xFF;
            }
        }
        while(image.size() <= int2(512)) image=upsample(image);
        Window& window = *(new Window(new ImageWidget(image),int2(-1),"Poisson"));
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
    }
} test;
