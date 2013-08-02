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
    real operator[](uint i) const { assert(i<3*3*3); return phi[i]; }
    real& operator[](uint i) { assert(i<3*3*3); return phi[i]; }
    real operator()(uint dx, uint dy, uint dz) const { assert(dx<3 && dy<3 && dz<3,int(dx),int(dy),int(dz)); return phi[dz*3*3+dy*3+dx]; }
    real& operator()(uint dx, uint dy, uint dz) { assert(dx<3 && dy<3 && dz<3,int(dx),int(dy),int(dz)); return phi[dz*3*3+dy*3+dx]; }
};

struct Test : Widget {
    int3 gridSize = int3(33,33,33);
    Grid<Cell> source{gridSize};
    Grid<Cell> target{gridSize};

    Window window{this, int2(1024), "3D Cylinder"_};
    Test() {
        initialize();
        window.backgroundColor=window.backgroundCenter=1;
        window.show();
        window.localShortcut(Escape).connect([]{exit();});
    }

    const real W[3] = {1./6, 4./6, 1./6}; // Lattice weight kernel
    real w[cb(3)]; // Explicit 3D Lattice weights
    vec3 v[cb(3)]; // Lattice velocities

    // Physical constants
    const real rho = 1e3; // Mass density: ρ[water] [kg/m³]
    const real nu = 1e-6; // Kinematic viscosity: ν[water] [m²/s]
    const real eta = rho * nu; // Dynamic viscosity: η[water] [Pa·s=kg/(m·s)]
    const vec3 g = vec3(0,0,9.8); // Body Force: Earth gravity pull [m/s²]

    // Physical parameters
    const real vmax = 70e-6; // Channel peak speed: v [m/s]
    const real R = sqrt(4*vmax*eta/(rho*g.z)); // Channel diameter: D [m]
    const real dx = 2*R/(gridSize.x+0.5);
    const real dxP = (rho * g.z * dx) / dx;  // Pressure gradient (~ applied pressure difference / thickness of the medium) (Assumes constant rho (incompressible flow))

    // Lattice parameters
    const real dt = sq(dx)/nu; // Time step: δx² α νδt [s]
    const real c = dx/dt; // Lattice velocity δx/δt [m/s]
    const real e = sq(c)/3; // Lattice internal energy density (e/c² = 1/3 optimize stability) [m²/s²]
    const real tau = nu / e; // Relaxation time: τ [s] = ν [m²/s] / e [m²/s²]
    const real alpha = dt/tau; // Relaxation coefficient: α = δt/τ [1]

    uint64 t = 0;

    void initialize() {
        log("dx=",dx*1e6,"μm", "dt=",dt*1e6,"μs");
        assert(alpha<1, alpha);
        for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) {
            w[dz*3*3+dy*3+dx] = W[dx]*W[dy]*W[dz];
            v[dz*3*3+dy*3+dx] = c * vec3(dx-1,dy-1,dz-1);
        }
        for(int z: range(gridSize.z)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            Cell& cell = source(x,y,z);
            for(int i: range(3*3*3)) cell[i] = rho / cb(3) / w[i];
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
                    vec3 pu = 0;
                    for(int i: range(3*3*3)) {
                        real wphi = w[i] * cell[i];
                        rho += wphi;
                        pu += wphi * v[i];
                    }
                    vec3 u = (dt/2)*g + pu/rho;
                    for(int i: range(3*3*3)) { // Relaxation
                        const real dotvu = dot(v[i],u); // m²/s²
                        const real phieq = rho * ( 1 + dotvu/e + sq(dotvu)/(2*sq(e)) - sq(u)/(2*e)); // kg/m/s²
                        const real BGK = (1-alpha)*cell[i] + alpha*phieq; //BGK relaxation
                        const vec3 k = rho * ( 1/sqrt(e) * (v[i]-u) + dotvu/sqrt(cb(e)) * v[i] );
                        const real body = dt/sqrt(e)*(1-dt/tau)*dot(k,g); // External body force
                        target(x,y,z)[i] = BGK + body;
                    }
                }
            }
        });
        // Streaming
        parallel(gridSize.z, [this](uint, uint z) {
            for(int y: range(gridSize.y)) {
                for(int x: range(gridSize.x)) {
                    for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) { //TODO: lookup map
                        int sx=x-(dx-1), sy=y-(dy-1), sz=z-(dz-1);
                        int tx=dx-1, ty=dy-1, tz=dz-1;
                        const int cx = (gridSize.x-1)/2, cy = (gridSize.y-1)/2;
                        const uint R2 = sq(min(cx,cy));
                        const uint r2 = sq(sx-cx) + sq(sy-cy);
                        if(r2>R2) { sx=x, sy=y, sz=z, tx=-tx, ty=-ty, tz=-tz; } // No slip vertical boundary
                        assert(!(sx<0 || sx>=gridSize.x || sy<0 || sy>= gridSize.y), sx, sy, r2, R2, cx, cy, sx-cx, sy-cy);
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
        vec3 U[8] = 0; real N[8] = {};
        parallel(gridSize.z, [&](uint id, uint z) {
            for(int y: range(gridSize.y)) {
                for(int x: range(gridSize.x)) {
                    const int cx = (gridSize.x-1)/2, cy = (gridSize.y-1)/2;
                    const uint R2 = sq(min(cx,cy));
                    const uint r2 = sq(x-cx) + sq(y-cy);
                    if(r2>R2) continue; // Solid

                    const Cell& cell = source(x,y,z);
                    real rho = 0;
                    vec3 pu = 0;
                    for(int i: range(3*3*3)) {
                        real wphi = w[i] * cell[i];
                        rho += wphi;
                        pu += wphi * v[i];
                    }
                    vec3 u = (dt/2)*g + pu/rho;
                    U[id] += u;
                    N[id]++;
                }
            }
        });
        vec3 u=0; real n=0; for(uint i: range(8)) u+=U[i], n+=N[i]; u/=n; // Mean speed (~superficial fluid flow rate (m³/s)/m²)
        log(u.z*1e6, vmax/2*1e6);
        real k = u.z * eta / dxP; // Permeability [m²] (1D ~ µm²)
        log(k*1e12, (vmax/2) * eta / dxP * 1e12);

        NonUniformSample analytic, numeric;
        {int z=gridSize.z/2, y=gridSize.y/2; for(int x: range(gridSize.x)) {
                const Cell& cell = source(x,y,z);
                real rho = 0; vec3 pu = 0;
                for(int i: range(3*3*3)) {
                    real wphi = w[i] * cell[i];
                    rho += wphi;
                    pu += wphi * v[i];
                }
                vec3 u = (dt/2)*g + pu/rho;
                numeric.insert(x*dx*1e6, u.z*1e6);
                const real d = 1./sqrt(2.);
                const real R = (gridSize.x-1+d) / 2 * dx;
                const real r = abs((gridSize.x-1+d)/2 - (x+d/2)) * dx;
                const real v = 1/(4*eta) * dxP * (sq(R)-sq(r)); // v = -1/(4η)·dxP·(R²-r²) (Poiseuille equation)
                analytic.insert(x*dx*1e6, v*1e6);
                //if(x==0) log(v/u.z);
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
