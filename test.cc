#include "thread.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "plot.h"

typedef vector<xyz,double,3> vec3x64; // Double precision vector (FIXME: normalize units and use float)
#define vec3 vec3x64

template<Type T> struct Grid {
    Grid(int3 size):size(size),cells(size.x*size.y*size.z){}
    T& operator()(uint x, uint y, uint z) { assert(x<size.x && y<size.y && z<size.z, x,y,z); return cells[z*size.y*size.x+y*size.x+x]; }
    int3 size;
    buffer<T> cells;
};

struct Cell {
    real phi[3*3*3]={}; // Mass density (kg/m³) for each lattice velocity
    real operator()(uint dx, uint dy, uint dz) const { assert(dx<3 && dy<3 && dz<3,int(dx),int(dy),int(dz)); return phi[dz*3*3+dy*3+dx]; }
    real& operator()(uint dx, uint dy, uint dz) { assert(dx<3 && dy<3 && dz<3,int(dx),int(dy),int(dz)); return phi[dz*3*3+dy*3+dx]; }
};

struct Test : Widget {
    int3 gridSize = 64;
    Grid<Cell> source{gridSize};
    Grid<Cell> target{gridSize};

    Window window{this, int2(1024), "2D Channel"_};
    Test() {
        initialize();
        window.backgroundColor=window.backgroundCenter=1;
        window.show();
        window.localShortcut(Escape).connect([]{exit();});
    }

    const real rho = 1e3; // Mass density: ρ[water] [kg/m³]
    const real nu = 1e-6; // Kinematic viscosity: ν[water] [m²/s]
    const real eta = rho * nu; // Dynamic viscosity: η[water] [Pa·s=kg/(m·s)]
    const vec3 g = vec3(0,0,9.8); // Body Force: Earth gravity pull [m/s²]

    const real v = 70e-6; // Channel peak speed: v [m/s]
    const real R = sqrt(2*v*eta/(rho*g.z)); // Channel diameter: D [m]
    const real dx = 2*R/(gridSize.x+0.5);

    const real dt = sq(dx)/nu; // Time step: δx² α νδt [s]
    const real c = dx/dt; // Lattice velocity δx/δt [m/s]
    const real e = sq(c)/3; // Lattice internal energy density (e/c² = 1/3 optimize stability) [m²/s²]
    const real tau = nu / e; // Relaxation time: τ [s] = ν [m²/s] / e [m²/s²]
    const real alpha = dt/tau; // Relaxation coefficient: α = δt/τ [1]

    uint64 t = 0;

    void initialize() {
        log("dx=",dx*1e6,"μm", "dt=",dt*1e6,"μs");
        assert(alpha<1, alpha);
        for(int z: range(gridSize.z)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            Cell& cell = source(x,y,z);
            for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) {
                const real W[] = {1./(2*27*8), 2./(27*8), 1./(2*27*8)};
                const real w = W[dx]*W[dy]*W[dz];
                cell(dx,dy,dz) = rho/27 / w;
            }
        }
        t = 0;
    }

    void step() {
        //Time time;
        // Collision
        parallel(gridSize.z, [this](uint, uint z) {
            for(int y: range(gridSize.y)) {
                for(int x: range(gridSize.x)) {
                    const Cell& cell = source(x,y,z);
                    real rho = 0;
                    for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) {
                        const real W[] = {1./(2*27*8), 2./(27*8), 1./(2*27*8)};
                        const real w = W[dx]*W[dy]*W[dz];
                        const real phi = cell(dx,dy,dz);
                        rho += w * phi;
                    }
                    vec3 u = dt/2*g;
                    for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) {
                        const real W[] = {1./(2*27*8), 2./(27*8), 1./(2*27*8)};
                        const real w = W[dx]*W[dy]*W[dz];
                        const vec3 v = c * vec3(dx-1,dy-1,dz-1);
                        const real phi = cell(dx,dy,dz);
                        u += w * v * phi / rho;
                    }
                    for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) { // Relaxation
                        const vec3 v = c * vec3(dx-1,dy-1,dz-1);
                        const real dotvu = dot(v,u); // m²/s²
                        const real phi = cell(dx,dy,dz);
                        const real phieq = rho * ( 1 + dotvu/e + sq(dotvu)/(2*sq(e)) - sq(u)/(2*e)); // kg/m/s²
                        const real BGK = (1-alpha)*phi + alpha*phieq; //BGK relaxation
                        const vec3 k = rho * ( 1/sqrt(e) * (v-u) + dotvu/sqrt(cb(e)) * v );
                        const real body = dt/sqrt(e)*(1-dt/tau)*dot(k,g); // External body force
                        target(x,y,z)(dx,dy,dz) = BGK + body;
                    }
                }
            }
        });
        // Streaming
        parallel(gridSize.z, [this](uint, uint z) {
            for(int y: range(gridSize.y)) {
                for(int x: range(gridSize.x)) {
                    for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) {
                        int sx=x-(dx-1), sy=y-(dy-1), sz=z-(dz-1);
                        int tx=dx-1, ty=dy-1, tz=dz-1;
                        if(sx<0 || sx>=gridSize.x || sy<0 || sy>=gridSize.y) { sx=x, sy=y, sz=z, tx=-tx, ty=-ty, tz=-tz; } // No slip vertical boundary (FIXME: cylinder)
                        if(sz<0) sz=gridSize.z-1; // Periodic top horizontal boundary
                        if(sz>=gridSize.z) sz=0; // Periodic bottom horizontal boundary
                        source(x,y,z)(dx,dy,dz) = target(sx,sy,sz)(tx+1,ty+1,tz+1);
                    }
                }
            }
        });
        t++;
        //log(str(dt*1000/(int)time)+"x RT"_);
        window.render();
    }

    void render(int2 position, int2 size) {
        NonUniformSample analytic, numeric;
        {int z=gridSize.z/2, y=gridSize.y/2; for(int x: range(gridSize.x)) {
                const Cell& cell = source(x,y,z);
                real rho = 0;
                for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) { // Cell density
                    const real W[] = {1./(2*27*8), 2./(27*8), 1./(2*27*8)};
                    const real w = W[dx]*W[dy]*W[dz];
                    const real phi = cell(dx,dy,dz);
                    rho += w * phi;
                }
                vec3 u = dt/2*g;
                for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) { // Cell velocity
                    const real W[] = {1./(2*27*8), 2./(27*8), 1./(2*27*8)};
                    const real w = W[dx]*W[dy]*W[dz];
                    const vec3 v = c * vec3(dx-1,dy-1,dz-1);
                    const real phi = cell(dx,dy,dz);
                    u += w * v * phi / rho;
                }
                numeric.insert(x*dx*1e6, u.y*1e6);
                const real dxP = rho * g.z * dx / dx;
                const real R = (gridSize.x+0.5) / 2 * dx;
                const real r = abs((gridSize.x+0.5)/2 - (x+1.5/2)) * dx;
                const real v = 1/(2*eta) * dxP * (sq(R)-sq(r)); // v = -1/(Cη)·dxP·(R²-r²) (Poiseuille equation (channel: C=2, cylinder: C=4))
                analytic.insert(x*dx*1e6, v*1e6);
            }
        }
        Plot plot;
        plot.title = str(int(round(t*dt*1e6)),"μs"), plot.xlabel=String("x [μm]"_), plot.ylabel=String("v [μm/s]"_);
        plot.legends << String("numeric"_); plot.dataSets << move(numeric);
        plot.legends << String("analytic"_); plot.dataSets << move(analytic);
        plot.render(position, size);
        step();
    }
} test;
