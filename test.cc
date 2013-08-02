#include "thread.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "plot.h"
#include "volume.h"

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
    const int3 gridSize = 252;
    Grid<int> solid{gridSize};
    Grid<Cell> source{gridSize};
    Grid<Cell> target{gridSize};

    Window window{this, 2*int2(gridSize.x, gridSize.z), "3D Cylinder"_};
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
    const real dx = 2.96e-6; // Spatial resolution [m]
    const real dxP = (rho * g.z * dx) / dx;  // Pressure gradient (~ applied pressure difference / thickness of the medium) (Assumes constant rho (incompressible flow))

    // Lattice parameters
    const real dt = sq(dx)/nu; // Time step: δx² α νδt [s]
    const real c = dx/dt; // Lattice velocity δx/δt [m/s]
    const real e = sq(c)/3; // Lattice internal energy density (e/c² = 1/3 optimize stability) [m²/s²]
    const real tau = nu / e; // Relaxation time: τ [s] = ν [m²/s] / e [m²/s²]
    const real alpha = dt/tau; // Relaxation coefficient: α = δt/τ [1]

    uint64 t = 0;

    void initialize() {
        Volume16 volume;
        parseVolumeFormat(volume, "256x256x128+2+2+2-193-tiled-squared"_);
        volume.data = readFile("/ptmp/Berea-2960-maximum.256x256x128+2+2+2-193-tiled-squared"_);
        for(int z: range(gridSize.z/2)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) { // Mirror volume
            solid(x,y,gridSize.z-1-z) = solid(x,y,z) = /*x<2||y<2||z<2||x>=gridSize.x-2||y>=gridSize.y-2||z>=gridSize.z-2||*/volume(x+2,y+2,z+2)==0 ? 1 : 0;
        }

        log("dx=",dx*1e6,"μm", "dt=",dt*1e6,"μs", "c=",c,"m/s");
        assert(alpha<1, alpha);
        for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) {
            w[dz*3*3+dy*3+dx] = W[dx]*W[dy]*W[dz];
            v[dz*3*3+dy*3+dx] = c * vec3(dx-1,dy-1,dz-1);
        }
        for(int z: range(gridSize.z)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            Cell& cell = source(x,y,z);
            /*if(solid(x,y,z)) for(int i: range(3*3*3)) cell[i] = 0;
            for(int i: range(3*3*3)) cell[i] = rho / cb(3) / w[i];*/
            for(int i: range(3*3*3)) cell[i] = 0;
            cell(1,1,1) = rho; // Starts at rest
        }
        t = 0;
    }

    void step() {
        //Time time;
        // Collision
        parallel(gridSize.z, [this](uint, uint z) {
            for(int y: range(gridSize.y)) {
                for(int x: range(gridSize.x)) {
                    if(solid(x,y,z)) continue;
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
                    if(solid(x,y,z)) continue;
                    for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) { //TODO: lookup map, tiled
                        int sx=x-(dx-1), sy=y-(dy-1), sz=z-(dz-1);
                        int tx=dx-1, ty=dy-1, tz=dz-1;
                        if(sz<0) sz=gridSize.z-1; // Periodic top horizontal boundary
                        if(sz>=gridSize.z) sz=0; // Periodic bottom horizontal boundary
                        if(sx<0 || sy<0 || sx>=gridSize.x || sy>=gridSize.y || solid(sx,sy,sz)) { sx=x, sy=y, sz=z, tx=-tx, ty=-ty, tz=-tz; }
                        source(x,y,z)(dx,dy,dz) = target(sx,sy,sz)(tx+1,ty+1,tz+1);
                    }
                }
            }
        });
        t++;
        //log(str(dt*1000/(int)time)+"x RT"_);
        window.render();
    }

    float sliceZ = 0.5;
    bool mouseEvent(int2 cursor, int2 size, Event, Button) { sliceZ = clip(0.f, float(cursor.x)/size.x, 1.f); return false; }

    void render(int2 position, int2 size) {
        vec3 U[8] = 0; real P[8] = {}; real N[8] = {};
        parallel(gridSize.z, [&](uint id, uint z) {
            for(int y: range(gridSize.y)) {
                for(int x: range(gridSize.x)) {
                    if(solid(x,y,z)) continue;

                    const Cell& cell = source(x,y,z);
                    real rho = 0;
                    vec3 pu = 0;
                    for(int i: range(3*3*3)) {
                        real wphi = w[i] * cell[i];
                        rho += wphi;
                        pu += wphi * v[i];
                    }
                    vec3 u = (dt/2)*g + pu/rho;
                    N[id]++;
                    P[id] += rho;
                    U[id] += u;
                }
            }
        });
        real n=0; real p=0; vec3 u=0; for(uint i: range(8)) p+=P[i], u+=U[i], n+=N[i]; p/=n; u/=n; // Mean speed (~superficial fluid flow rate (m³/s)/m²)
        real k = u.z * eta / dxP; // Permeability [m²] (1D ~ µm²)
        log(p, ftoa(u.z / c,3),"c", u.z*1e6,"μm/s", k*1e15, "mD");
        vec3 meanU = u;

        const int64 X=gridSize.x, Y=gridSize.y;
        assert_(2*gridSize.xy()==size);
        int z = sliceZ * (gridSize.z-1);
        assert_(z>=0 && z<gridSize.z);
        for(int y: range(Y)) for(int x: range(X)) {
            vec3 u=0;
            if(!solid(x,y,z)) {
                const Cell& cell = source(x,y,z);
                real rho = 0; vec3 pu = 0;
                for(int i: range(3*3*3)) {
                    real wphi = w[i] * cell[i];
                    rho += wphi;
                    pu += wphi * v[i];
                }
                u = (dt/2)*g + pu/rho;
            }
            uint linear = clip(0,(int)round(0xFF*u.z/meanU.z),0xFF);
            extern uint8 sRGB_lookup[256];
            uint sRGB = sRGB_lookup[linear];
            for(int dy: range(2)) for(int dx: range(2)) framebuffer((position.x+x)*2+dx,(position.y+y)*2+dy) = byte4(sRGB,sRGB,sRGB,0xFF);
        }
        step();
    }
} test;
