#include "thread.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "plot.h"
#include "volume.h"
#include "simd.h"
#define real float

constexpr int3 gridSize {512,512,512};

// from volume.cc
/// Interleaves bits
static uint64 interleave(uint64 bits, uint64 offset, uint stride=3) { uint64 interleavedBits=0; for(uint b=0; bits!=0; bits>>=1, b++) interleavedBits |= (bits&1) << (b*stride+offset); return interleavedBits; }
/// Generates lookup tables of interleaved bits
static buffer<uint> interleavedLookup(uint size, uint offset, uint stride=3) { buffer<uint> lookup(size); for(uint i=0; i<size; i++) { lookup[i]=interleave(i,offset,stride); } return lookup; }
buffer<uint> offsetX = interleavedLookup(gridSize.x,0), offsetY = interleavedLookup(gridSize.y,1), offsetZ = interleavedLookup(gridSize.z,2); // Offset lookup tables to tile grid

template<Type T> struct Grid {
    Grid():cells(gridSize.x*gridSize.y*gridSize.z){}
    T& operator()(uint x, uint y, uint z) { assert(x<gridSize.x && y<gridSize.y && z<gridSize.z, x,y,z); return cells[offsetZ[z]+offsetY[y]+offsetX[x]]; }
    buffer<T> cells;
};

typedef real Cell[28]; // Mass density (kg/m³) for each lattice velocity
typedef uint ICell[27]; // Index lookup to stream

// Physical constants
constexpr real rho = 1e3; // Mass density: ρ[water] [kg/m³]
constexpr real nu = 1e-6; // Kinematic viscosity: ν[water] [m²/s]
constexpr real eta = rho * nu; // Dynamic viscosity: η[water] [Pa·s=kg/(m·s)]
constexpr vec3 g = vec3(0,0,9.8); // Body Force: Earth gravity pull [m/s²]

// Physical parameters
constexpr real dx = 0.74e-6; // Spatial resolution [m]
constexpr real dxP = rho * g.z;  // Pressure gradient (applied pressure difference / thickness of the medium = (F/S)/δx = (ρgx³/x²)/x) (Assumes constant rho (incompressible flow))

// Lattice parameters
constexpr real dt = sq(dx)/nu; // Time step: δx² α νδt [s]
constexpr real c = dx/dt; // Lattice velocity δx/δt [m/s]
constexpr real E = sq(c)/3; // Lattice internal energy density (e/c² = 1/3 optimize stability) [m²/s²]
constexpr real tau = nu / E; // Relaxation time: τ [s] = ν [m²/s] / e [m²/s²]
constexpr real alpha = dt/tau; // Relaxation coefficient: α = δt/τ [1]
static_assert(alpha<1, "");

constexpr real W[3] = {1./6, 4./6, 1./6}; // Lattice weight kernel
#if 0// Cannot be constexpr'ed
static void __attribute((constructor(20000))) computeWeights() {
    {String s; for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) { s<<"W["_+str(dx)+"]*W["_+str(dy)+"]*W["_+str(dz)+"], "_; } log(s);}
    {String s; for(int unused dz: range(3)) for(int unused dy: range(3)) for(int dx: range(3)) s<<str(dx-1)+","_; log(s);}
    {String s; for(int unused dz: range(3)) for(int dy: range(3)) for(int unused dx: range(3)) s<<str(dy-1)+","_; log(s);}
    {String s; for(int dz: range(3)) for(int unused dy: range(3)) for(int unused dx: range(3)) s<<str(dz-1)+","_; log(s);}
}
#endif
constexpr real w[3*3*3] = {
        W[0]*W[0]*W[0], W[1]*W[0]*W[0], W[2]*W[0]*W[0], W[0]*W[1]*W[0], W[1]*W[1]*W[0], W[2]*W[1]*W[0], W[0]*W[2]*W[0], W[1]*W[2]*W[0], W[2]*W[2]*W[0],
        W[0]*W[0]*W[1], W[1]*W[0]*W[1], W[2]*W[0]*W[1], W[0]*W[1]*W[1], W[1]*W[1]*W[1], W[2]*W[1]*W[1], W[0]*W[2]*W[1], W[1]*W[2]*W[1], W[2]*W[2]*W[1],
        W[0]*W[0]*W[2], W[1]*W[0]*W[2], W[2]*W[0]*W[2], W[0]*W[1]*W[2], W[1]*W[1]*W[2], W[2]*W[1]*W[2], W[0]*W[2]*W[2], W[1]*W[2]*W[2], W[2]*W[2]*W[2]
};
constexpr real vx[28] = {-1,0,1,-1,0,1,-1,0,1,-1,0,1,-1,0,1,-1,0,1,-1,0,1,-1,0,1,-1,0,1,0};
constexpr real vy[28] = {-1,-1,-1,0,0,0,1,1,1,-1,-1,-1,0,0,0,1,1,1,-1,-1,-1,0,0,0,1,1,1,0};
constexpr real vz[28] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0};

struct Test : Widget {
    Grid<ICell> streamLookup;
    Grid<Cell> source;
    buffer<real> target {gridSize.x*gridSize.y*gridSize.z*28ul};

    Window window{this, /*int2(512)*/int2(640,480), "3D Cylinder"_};
    Test() {
        window.backgroundColor=window.backgroundCenter=1;
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
        initialize();
    }

    uint N = 0;
    uint t = 0;
    uint profileStartStep=0; Time totalTime;
    real meanV = 1;
    NonUniformSample permeability;

    void initialize() {
#if 1 // Import pore space from volume data file
        Volume16 volume;
        const string path = arguments()[0];
        parseVolumeFormat(volume, section(path,'.',-2,-1));
        assert_(volume.margin==int3(0) && volume.sampleCount==int3(gridSize.x,gridSize.y,gridSize.z/2));
        Map map(path, currentWorkingDirectory());
        volume.data = buffer<byte>(map);
        for(int z: range(gridSize.z)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) { // Setup stream lookup
            bool solid = volume(x,y,z<gridSize.z/2?z:gridSize.z-1-z)==0; // Mirror
            if(!solid) {
                N++;
                for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) {
                    int sx=x-(dx-1), sy=y-(dy-1), sz=z-(dz-1);
                    sz &= (512-1); // Periodic top/bottom horizontal boundary
                    uint index=dz*3*3+dy*3+dx, sIndex=index; //unrolled
                    uint offset = ::offsetX[x]+::offsetY[y]+::offsetZ[z]; // TODO: compress
                    if(sx<0 || sy<0 || sx>=gridSize.x || sy>=gridSize.y || volume(sx,sy,sz<gridSize.z/2?sz:gridSize.z-1-sz)==0) sIndex=26-index;
                    else offset = ::offsetX[sx]+::offsetY[sy]+::offsetZ[sz];  // TODO: compress
                    assert_(offset*28+sIndex>0); // 0 is reserved to flag solid voxels (FIXME: compress)
                    streamLookup(x,y,z)[index] = offset*28+sIndex; //TODO: compress
                }
            } else {
                for(uint index: range(27)) streamLookup(x,y,z)[index] = 0;
            }
        }
#else // Initialize pore space to tube
        const int cx = (gridSize.x-1)/2, cy = (gridSize.y-1)/2;
        const uint R = min(16,min(cx,cy));
        for(int z: range(gridSize.z/2)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            const uint r2 = sq(x-cx) + sq(y-cy);
            solid(x,y,gridSize.z-1-z) = solid(x,y,z) = r2>R*R ? 1 : (N++, 0);
        }
        real meanV_tube = dxP/(8*eta)*sq(R*dx);
        real epsilon_tube = PI*R*R/(gridSize.x*gridSize.y);
        real k_tube = epsilon_tube * meanV_tube * eta / dxP; // Permeability [m²] (1D ~ µm²) // εu ~ superficial fluid flow rate (m³/s)/m²)
        log(meanV_tube*1e6,"μm/s", k_tube*1e15,"mD");
#endif
        for(int z: range(gridSize.z)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) {
            Cell& cell = source(x,y,z);
            for(int i: range(3*3*3)) cell[i] = 0;
            cell[1*3*3+1*3+1] = rho / w[1*3*3+1*3+1]; // Starts at rest
        }
        t = 0;
        log("dx=",dx*1e6,"μm", "dt=",dt*1e6,"μs", "c=",c,"m/s","ε=",real(N)/real(gridSize.x*gridSize.y*gridSize.z));
    }

    tsc total, collide, stream, other;
    void step() {
        if(t==0) log("Startup",totalTime);
        if(t==1) { profileStartStep=t; totalTime.reset(); total.start(); collide.reset(); stream.reset(); other.reset(); } // Resets after first timesteps to avoid inaccurate steady state timing profile (TODO: moving average)
        if(other) other.stop();
        // Collision (43%)
        collide.start();
        parallel(gridSize.z, [this](uint, uint z) {
            const ICell* streamLookup = this->streamLookup.cells;
            const Cell* source = this->source.cells;
            real* target = this->target.begin();
            uint offsetZ = ::offsetZ[z];
            for(int y: range(gridSize.y)) {
                uint offsetZY = offsetZ + ::offsetY[y];
                for(int x: range(gridSize.x)) {
                    uint offset = offsetZY + ::offsetX[x];
                    if(!streamLookup[offset]) continue;
                    const Cell& cell = source[offset];
                    real rho = 0, pux=0, puy=0, puz=0;
                    for(int i: range(3*3*3)) { //TODO: SIMD
                        real wphi = w[i] * cell[i];
                        rho += wphi;
                        pux += wphi * vx[i];
                        puy += wphi * vy[i];
                        puz += wphi * vz[i];
                    }
                    constexpr real gz = (dt/(2*c))*g.z;
                    real ux = pux/rho, uy = puy/rho, uz = puz/rho + gz, sqU = ux*ux+uy*uy+uz*uz;
                    for(int i=0; i<28; i+=4) { // Relaxation
                        const v4sf dotvu = float4(ux) * loada(vx+i) + float4(uy) * loada(vy+i) + float4(uz) * loada(vz+i); // m²/s²
                        static constexpr v4sf a1 = float4(sq(c)/E), a2 = float4(sq(sq(c)/E)/2), a3 = float4(sq(c)/(2*E));
                        const v4sf phieq = rho * (1 + a1 * dotvu + a2 * sq(dotvu) - a3 * float4(sqU)); // kg/m/s²
                        static constexpr v4sf A = float4(alpha), iA = float4(1-alpha);
                        const v4sf BGK = iA * loada(cell+i) + A * phieq; //BGK relaxation
                        constexpr float b0 = dt/sqrt(E)*(1-dt/tau)*c*g.z;
                        static constexpr v4sf b1 = float4(b0/sqrt(E)), b2 = float4(b0*sq(c)/sqrt(cb(E)));
                        const v4sf body = float4(rho) * ( b1 * (loada(vz+i)-float4(uz)) + b2 * dotvu * loada(vz+i) ); // External body force
                        storea(target+offset*28+i, BGK + body);
                    }
                }
            }
        });
        collide.stop();
        // Streaming (55%)
        real U[8]={};
        stream.start();
        parallel(gridSize.z, [this,&U](uint id, uint z) { //TODO: ZOrder
            const ICell* streamLookup = this->streamLookup.cells;
            Cell* source = this->source.cells.begin();
            const real* target = this->target;
            real Ui=0;
            uint offsetZ = ::offsetZ[z];
            for(int y: range(gridSize.y)) {
                uint offsetZY = offsetZ + ::offsetY[y];
                for(int x: range(gridSize.x)) {
                    uint offset = offsetZY + ::offsetX[x];
                    if(!streamLookup[offset]) continue;
                    real rho = 0; real pu = 0;
                    for(uint index: range(27)) {
                        real phi = target[streamLookup[offset][index]]; //Gather
                        //TODO: SIMD
                        source[offset][index] = phi;
                        real wphi = w[index] * phi;
                        rho += wphi;
                        pu += wphi * vz[index];
                    }
                    real u = pu/rho;
                    Ui += u;
                }
            }
            U[id] += Ui;
        });
        stream.stop();
        real u=0; for(uint i: range(8)) u+=U[i]; u = c*( (dt/(2*c))*g.z + u/N); // Mean speed
        meanV = u;
        real epsilon = real(N)/real(gridSize.x*gridSize.y*gridSize.z); // Porosity ε
        real k = epsilon * meanV * eta / dxP; // Permeability [m²] (1D ~ µm²) // εu ~ superficial fluid flow rate (m³/s)/m²)
        t++;
        permeability.insert(t, k*1e15);
        log(t, totalTime/(t-profileStartStep), "ms", meanV*1e6,"μm/s", k*1e15,"mD", total?str("(Collide:", str(round(100.0*collide/total))+"%"_, "Stream:", str(round(100.0*stream/total))+"%"_, "Other:", str(round(100.0*other/total))+"%)"_):""_);
        other.start();
        window.render();
    }

#if 0 // Slice
    float sliceZ = 0.5;
    bool mouseEvent(int2 cursor, int2 size, Event, Button) { sliceZ = clip(0.f, float(cursor.x)/size.x, 1.f); return false; }

    void render(int2 position, int2 size) {
        constexpr int upscale=1; assert_(upscale*gridSize.xy()==size);
        const int64 X=gridSize.x, Y=gridSize.y;
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
                    pu += wphi * c * v[i];
                }
                u = (dt/2)*g + pu/rho;
            }
            uint linear = clip(0,(int)round(0xFF*u.z/meanV),0xFF);
            extern uint8 sRGB_lookup[256];
            uint sRGB = sRGB_lookup[linear];
            for(int dy: range(upscale)) for(int dx: range(upscale)) framebuffer((position.x+x)*upscale+dx,(position.y+y)*upscale+dy) = byte4(sRGB,sRGB,sRGB,0xFF);
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
        if(t<256) step();
    }
#endif

} test;
