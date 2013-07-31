#include "thread.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "plot.h"

typedef vector<xy,double,2> vec2x64; // Double precision vector
#define vec2 vec2x64

template<Type T> struct Grid {
    Grid(int2 size):size(size),cells(size.x*size.y){}
    T& operator()(uint x, uint y) { assert(x<size.x && y<size.y, x,y); return cells[y*size.x+x]; }
    int2 size;
    buffer<T> cells;
};

struct Cell {
    real phi[3*3]={};
    real operator()(uint dx, uint dy) const { assert(dx<3 && dy<3,int(dx),int(dy)); return phi[dy*3+dx]; }
    real& operator()(uint dx, uint dy) { assert(dx<3 && dy<3,int(dx),int(dy)); return phi[dy*3+dx]; }
};

struct Test : Widget {
    int2 gridSize{128,128};
    Grid<Cell> source{gridSize};
    Grid<Cell> target{gridSize};

    Window window{this, int2(1024), "2D Channel"_};
    Test() {
        initialize();
        window.backgroundColor=window.backgroundCenter=1;
        window.show();
        window.localShortcut(Escape).connect([]{exit();});
    }

    const real rho = 10e3; // Mass density: ρ[water] [kg/m³]
    const real nu = 10e-4/*0e-6*/; // Kinematic viscosity: ν[water] [m²/s]
#if 0
    const real dt = 1./60; // Time step: display rate [s]
    const real dx = sqrt(nu*dt); // Spatial step: δx² α νδt [m]
#else
    const real dx = (window.size.x/gridSize.x)/3700.; // Spatial step: cell size [px] / display resolution [px/m] [m]
    const real dt = sq(dx)/nu; // Time step: δx² α νδt [s]
#endif
    const real c = dx/dt; // Lattice velocity δx/δt [m/s]
    const real e = sq(c)/3; // Specific internal energy density c = √(3e) [m²/s²]
    const real tau = nu / e; // Relaxation time: τ [s] = ν [m²/s] / e [m²/s²]
    const real alpha = dt/tau; // Relaxation coefficient: α = δt/τ [1]
    const vec2 g = vec2(0,9.8); // Body Force: Earth gravity pull [m/s²]

    uint64 t = 0;

    void initialize() {
        log("dx=",dx*1000,"mm", "dt=",dt*1000,"ms", "("_+str(1./dt),"Hz)");
        assert(alpha<1, alpha);
        for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            Cell& cell = source(x,y);
            for(int dy: range(3)) for(int dx: range(3)) {
                const real W[] = {1./6, 2./3, 1./6};
                const real w = W[dx]*W[dy];
                cell(dx,dy) = rho/9 / w;
            }
        }
        t = 0;
    }

    void step() {
        Time time;
        // Collision
        //for(int y: range(gridSize.y)) {
        parallel(gridSize.y, [this](uint, uint y) {
            for(int x: range(gridSize.x)) {
                const Cell& cell = source(x,y);
                real rho = 0;
                for(int dy: range(3)) for(int dx: range(3)) { // Cell density
                    const real W[] = {1./6, 2./3, 1./6};
                    const real w = W[dx]*W[dy];
                    const real phi = cell(dx,dy);
                    rho += w * phi;
                }
                vec2 u = dt/2*g;
                for(int dy: range(3)) for(int dx: range(3)) { // Cell velocity
                    const real W[] = {1./6, 2./3, 1./6};
                    const real w = W[dx]*W[dy];
                    const vec2 v = c * vec2(dx-1,dy-1);
                    const real phi = cell(dx,dy);
                    u += w * v * phi / rho;
                }
                for(int dy: range(3)) for(int dx: range(3)) { // Relaxation
                    const vec2 v = c * vec2(dx-1,dy-1);
                    const real dotvu = dot(v,u); // m²/s²
                    const real phi = cell(dx,dy);
                    const real phieq = rho * ( 1 + dotvu/e + sq(dotvu)/(2*sq(e)) - sq(u)/(2*e)); // kg/m/s²
                    const real BGK = (1-alpha)*phi + alpha*phieq; //BGK relaxation
                    const vec2 k = rho * ( 1/sqrt(e) * (v-u) + dotvu/sqrt(cb(e)) * v );
                    const real body = dt/sqrt(e)*dot(k,g); // External body force
                    assert(BGK + body >= 0, BGK, body);
                    target(x,y)(dx,dy) = BGK + body;
                }
            }
        });
        // Streaming
        //for(int y: range(gridSize.y))
        parallel(gridSize.y, [this](uint, uint y) {
            for(int x: range(gridSize.x)) {
                for(int dy: range(3)) for(int dx: range(3)) {
                    int sx=x-(dx-1), sy=y-(dy-1);
                    if(sy<0) sy=gridSize.y-1; // Periodic horizontal boundary
                    if(sy>=gridSize.y) sy=0; // Periodic horizontal boundary
                    int tx=dx-1, ty=dy-1;
                    if(sx<0 || sx>=gridSize.x) { sx=x, tx=-tx, ty=-ty; } // No slip vertical boundary
                    source(x,y)(dx,dy) = target(sx,sy)(tx+1,ty+1);
                }
            }
        });
        t++;
        //log(str(dt*1000/(int)time)+"x RT"_);
        window.render();
    }

    void render(int2 position, int2 size) {
        NonUniformSample analytic, numeric;
        {int y=gridSize.y/2; for(int x: range(gridSize.x)) {
                const Cell& cell = source(x,y);
                real rho = 0;
                for(int dy: range(3)) for(int dx: range(3)) { // Cell density
                    const real W[] = {1./6, 2./3, 1./6};
                    const real w = W[dx]*W[dy];
                    const real phi = cell(dx,dy);
                    rho += w * phi;
                }
                vec2 u = dt/2*g;
                for(int dy: range(3)) for(int dx: range(3)) { // Cell velocity
                    const real W[] = {1./6, 2./3, 1./6};
                    const real w = W[dx]*W[dy];
                    const vec2 v = c * vec2(dx-1,dy-1);
                    const real phi = cell(dx,dy);
                    u += w * v * phi / rho;
                }
                numeric.insert(x*dx*100, u.y);
                const real eta = rho * nu;
                const real dxP = rho * g.y * dx / dx;
                const real R = gridSize.x * dx / 2;
                const real r = abs(gridSize.x/2 - x) * dx;
                const real v = 1/(4*eta) * dxP * (sq(R)-sq(r)); // v = -1/(4η)·dxP·(R²-r²) (Poiseuille equation)
                analytic.insert(x*dx*100, v);
            }
        }
        Plot plot;
        plot.title = str(int(round(t*dt*1000)),"ms"), plot.xlabel=String("x [cm]"_), plot.ylabel=String("v [m/s]"_);
        plot.legends << String("numeric"_); plot.dataSets << move(numeric);
        plot.legends << String("analytic"_); plot.dataSets << move(analytic);
        plot.render(position, size);
        step();
    }
} test;
