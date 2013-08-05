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
    const int3 gridSize = 512;
    Grid<int> solid{gridSize};
    Grid<Cell> source{gridSize};
    Grid<Cell> target{gridSize};

    Window window{this, int2(640,480), "3D Cylinder"_};
    Test() {
        initialize();
        step();
        window.backgroundColor=window.backgroundCenter=1;
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
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
    const real dx = 0.74e-6; // Spatial resolution [m]
    const real dxP = rho * g.z;  // Pressure gradient (applied pressure difference / thickness of the medium = (F/S)/δx = (ρgx³/x²)/x) (Assumes constant rho (incompressible flow))

    // Lattice parameters
    const real dt = sq(dx)/nu; // Time step: δx² α νδt [s]
    const real c = dx/dt; // Lattice velocity δx/δt [m/s]
    const real e = sq(c)/3; // Lattice internal energy density (e/c² = 1/3 optimize stability) [m²/s²]
    const real tau = nu / e; // Relaxation time: τ [s] = ν [m²/s] / e [m²/s²]
    const real alpha = dt/tau; // Relaxation coefficient: α = δt/τ [1]

    uint64 t = 0;
    NonUniformSample permeability;

    void initialize() {
#if 1 // Import pore space from volume data file
        Volume16 volume;
        const string path = arguments()[0];
        parseVolumeFormat(volume, section(path,'.',-2,-1));
        assert_(volume.margin==int3(0) && volume.sampleCount==int3(gridSize.x,gridSize.y,gridSize.z/2));
        Map map(path, currentWorkingDirectory());
        volume.data = buffer<byte>(map);
        for(int z: range(gridSize.z/2)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) { // Mirror volume
            solid(x,y,gridSize.z-1-z) = solid(x,y,z) = volume(x,y,z)==0 ? 1 : 0;
        }
#else // Initilize pore space to cylinder
        for(int z: range(gridSize.z/2)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            const uint r2 = sq(x-cx) + sq(y-cy);
            solid(x,y,gridSize.z-1-z) = solid(x,y,z) = r2>R*R;
        }
#endif

        log("dx=",dx*1e6,"μm", "dt=",dt*1e6,"μs", "c=",c,"m/s");
        assert(alpha<1, alpha);
        for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) {
            w[dz*3*3+dy*3+dx] = W[dx]*W[dy]*W[dz];
            v[dz*3*3+dy*3+dx] = c * vec3(dx-1,dy-1,dz-1);
        }
        for(int z: range(gridSize.z)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            Cell& cell = source(x,y,z);
            for(int i: range(3*3*3)) cell[i] = 0;
            cell(1,1,1) = rho / w[1*3*3+1*3+1]; // Starts at rest
        }
        t = 0;
        collide.reset(); stream.reset(); total.reset(); other.reset();
        total.start();
    }

    tsc collide, stream, average, other, total;
    void step() {
        if(other) other.stop();
        // Collision
        collide.start();
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
        collide.stop();
        // Streaming
        stream.start();
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
        stream.stop();
        // Average speed
        average.start();
        vec3 U[8]; real N[8]; for(uint i: range(8)) U[i]=0, N[i]=0;
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
                    U[id] += u;
                }
            }
        });
        real n=0; vec3 u=0; for(uint i: range(8)) u+=U[i], n+=N[i]; assert_(n); u/=n; // Mean speed
        real meanV = u.z;
        real epsilon = n/(gridSize.x*gridSize.y*gridSize.z); // Porosity ε
        real k = epsilon * meanV * eta / dxP; // Permeability [m²] (1D ~ µm²) // εu ~ superficial fluid flow rate (m³/s)/m²)
        t++;
        permeability.insert(t, k*1e15);
        average.stop();
        log(meanV*1e6,"μm/s", k*1e15,"mD", "(Collide:", str(100*collide/total)+"%"_, "Stream:", str(100*stream/total)+"%"_, "Average:", str(100*average/total)+"%"_, "Other:", str(100*other/total)+"%)"_);
        other.start();
        window.render();
    }

#if 0 // Slice
    float sliceZ = 0.5;
    bool mouseEvent(int2 cursor, int2 size, Event, Button) { sliceZ = clip(0.f, float(cursor.x)/size.x, 1.f); return false; }

    void render(int2 position, int2 size) {
        const int64 X=gridSize.x, Y=gridSize.y;
        assert_(2*gridSize.xy()==sizegridSize.xy(), size);
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
            uint linear = clip(0,(int)round(0xFF*u.z/meanV),0xFF);
            extern uint8 sRGB_lookup[256];
            uint sRGB = sRGB_lookup[linear];
            for(int dy: range(2)) for(int dx: range(2)) framebuffer((position.x+x)*2+dx,(position.y+y)*2+dy) = byte4(sRGB,sRGB,sRGB,0xFF);
        }
        step();
    }
#else // Plot
    void render(int2 position, int2 size) {
        Plot plot;
        plot.xlabel=String("t [step]"_), plot.ylabel=String("k [mD]"_);\
        plot.dataSets << move(permeability);
        plot.render(position, size);
        permeability = move(plot.dataSets.first());
        step();
    }
#endif

} test;
