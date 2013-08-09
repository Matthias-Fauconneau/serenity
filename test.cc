#include "thread.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "plot.h"
#include "volume.h"
#include "simd.h"

#define TUBE 0
#define SLICE 0

#define real float
constexpr int3 gridSize {512,512,512};

// from volume.cc
/// Pack interleaved bits
static uint pack(uint bits, uint offset, uint stride=3) { uint packedBits=0; bits>>=offset; for(uint b=0; bits!=0; bits>>=stride, b++) packedBits |= (bits&1) << b; return packedBits; }
/// Uninterleaves 3 coordinates
static int3 zOrder(uint index) { return int3(pack(index,0),pack(index,1),pack(index,2)); }

// Primary physical parameters
//constexpr real dx = 0.74e-6; // Spatial resolution: λ[berea] [m]
constexpr real dx = 2e-6; // Spatial resolution: λ[P2PA1_02] [m]
constexpr real nu = 1e-6; // Kinematic viscosity: ν[water] [m²/s]

// Lattice parameters
constexpr real dt = sq(dx)/nu; // Time step: δx² α νδt [s]
constexpr real c = dx/dt; // Lattice velocity δx/δt [m/s]
constexpr real E = sq(c)/3; // Lattice internal energy density (e/c² = 1/3 optimize stability) [m²/s²]
constexpr real tau = nu / E; // Relaxation time: τ [s] = ν [m²/s] / e [m²/s²]
constexpr real alpha = dt/tau; // Relaxation coefficient: α = δt/τ [1]
static_assert(alpha<1, "");

// Secondary physical parameters
constexpr real rho = 1e3; // Mass density: ρ[water] [kg/m³]
constexpr real eta = rho * nu; // Dynamic viscosity: η[water] [Pa·s=kg/(m·s)]
constexpr vec3 g = vec3(0,0,9.8); // Body Force: Earth gravity pull [m/s²] (~0.0001 dx/dt²)
constexpr real dxP = rho * g.z;  // Pressure gradient (applied pressure difference / thickness of the medium = (F/S)/δx = (ρgx³/x²)/x) (Assumes constant rho (incompressible flow))

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
    buffer<uint> latticeLookup{gridSize.x*gridSize.y*gridSize.z}; // Index from coordinates to compressed stream,source,target buffers (-1 = solid)
    static constexpr uint Solid = -1;
    buffer<uint> streamLookup; //[28N] Index into target to stream each lattice velocity
    buffer<real> source;//[28N] Mass density (kg/m³) for each lattice velocity (collision->stream)
    buffer<real> target;//[28N] Mass density (kg/m³) for each lattice velocity (stream->collision)

    Test() {
        window.backgroundColor=window.backgroundCenter=1;
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
        setup();
    }

    uint N = 0;
    uint t = 0;
    uint profileStartStep=0; Time totalTime;
    real meanV = 1; // in lattice unit speed [c]
    NonUniformSample permeability;

    void setup() {
        Volume16 volume;
        const string path = arguments()[0];
        TextData s(path); s.until('-'); uint resolution = s.integer(); assert_(resolution == round(dx*1e9), resolution, int(round(dx*1e9)));
        parseVolumeFormat(volume, section(path,'.',-2,-1));
        const int X=gridSize.x, Y=gridSize.y, Z=gridSize.z;
        const int marginX=volume.margin.x, marginY=volume.margin.z, marginZ=volume.margin.z;
        assert_(marginZ==0 && volume.sampleCount==int3(gridSize.x,gridSize.y,gridSize.z/2), volume.margin);
        Map map(path, currentWorkingDirectory());
        volume.data = buffer<byte>(map);
        Time latticeTime;
        for(int index : range(X*Y*Z)) { // Setup lattice lookup
            int3 xyz = zOrder(index); int x=xyz.x, y=xyz.y, z=xyz.z;
            uint offset = z*Y*X+y*X+x; // Simple layout (only used during setup)
#if !TUBE // Import pore space from volume data file
            if(x>=marginX && x<X-marginX && y>=marginY && y<Y-marginY && volume(x,y,z<gridSize.z/2?z:gridSize.z-1-z)) {
#else // Initialize pore space to tube
            const int cx = (gridSize.x-1)/2, cy = (gridSize.y-1)/2;
            const uint R = 64;
            const uint r2 = sq(x-cx) + sq(y-cy);
            if(r2<=R*R) {
#endif
                latticeLookup[offset] = N;
                N++;
            } else {
                latticeLookup[offset] = Solid;
            }
        }
        log("Lattice setup", latticeTime);
        assert_(N<1u<<31);
        streamLookup = buffer<uint>(28*N); //TODO: mmap
        source = buffer<real>(28*N); //TODO: mmap
        target = buffer<real>(28*N+1); //TODO: mmap
        target[28*N] = 0;
        Time streamTime;
        for(int z: range(gridSize.z)) for(int y: range(gridSize.y)) for(int x: range(gridSize.x)) { // Setup stream lookup
            uint offset = latticeLookup[z*Y*X+y*X+x];
            if(offset!=Solid) {
                for(int dz: range(3)) for(int dy: range(3)) for(int dx: range(3)) {
                    int sx=x-(dx-1), sy=y-(dy-1), sz=z-(dz-1);
                    if(sz<0) sz+= gridSize.z; // Periodic top/bottom horizontal boundary
                    if(sz>=gridSize.z) sz-= gridSize.z; // Periodic top/bottom horizontal boundary
                    uint index=dz*3*3+dy*3+dx, sIndex=index;
                    uint sOffset=offset;
                    if(sx<0 || sy<0 || sx>=gridSize.x || sy>=gridSize.y || latticeLookup[sz*Y*X+sy*X+sx]==Solid) sIndex=26-index;
                    else sOffset = latticeLookup[sz*Y*X+sy*X+sx];
                    streamLookup[offset*28+index] = sOffset*28+sIndex;
                }
                streamLookup[offset*28+3*3*3] = 28*N; // Load 0
                real* cell = &source[offset*28];
                for(int i: range(3*3*3)) cell[i] = 0;
                cell[1*3*3+1*3+1] = rho / w[1*3*3+1*3+1]; // Starts at rest
            }
        }
        log("Stream setup", streamTime);
#if !SLICE
        latticeLookup=buffer<uint>();
#endif
        t = 0;
        log("dx=",dx*1e6,"μm", "dt=",dt*1e6,"μs", "c=",c,"m/s","ε=",real(N)/real(gridSize.x*gridSize.y*gridSize.z),"g=",g.z);
    }

    tsc total, collide, stream, other;
    void step() {
        if(t==0) log("Startup",totalTime);
        if(t==1) { profileStartStep=t; totalTime.reset(); total.start(); collide.reset(); stream.reset(); other.reset(); } // Resets after first timesteps to avoid inaccurate steady state timing profile (TODO: moving average)
        if(other) other.stop();
        // Collision
        collide.start();
        chunk_parallel(N, [this](uint, uint start, uint size) {
            const real* source = this->source;
            real* target = this->target.begin();
            for(uint offset: range(start, start+size)) {
                const real* cell = source+offset*28;
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
        });
        collide.stop();
        // Streaming
        real U[8]={};
        stream.start();
        chunk_parallel(N, [this,&U](uint id, uint start, uint size) {
            const uint* streamLookup = this->streamLookup;
            real* source = this->source.begin();
            const real* target = this->target;
            v4sf Ui = _0f;
            for(uint offset: range(start, start+size)) {
                real* sourceCell = source + offset*28;
                const uint* streamCell = streamLookup + offset*28;
                v4sf rho = _0f; v4sf puz = _0f;
                for(int i=0; i<28; i+=4) { //TODO: 3*8+4
                    const uint* stream = streamCell + i;
                    const v4sf phi = {target[stream[0]], target[stream[1]],target[stream[2]],target[stream[3]]}; //Gather
                    //TODO: SIMD
                    storea(sourceCell+i, phi);
                    const v4sf wphi = loada(w+i) * phi;
                    rho += wphi;
                    puz += wphi * loada(vz+i);
                }
                Ui += sum(puz)/sum(rho);
            }
            U[id] += extractf(Ui, 0);
        });
        stream.stop();
        real u=0; for(uint i: range(8)) u+=U[i]; u = ( /*(dt/(2*c))*g.z +*/ u/N); // Mean speed
        meanV = u;
        real epsilon = real(N)/real(gridSize.x*gridSize.y*gridSize.z); // Porosity ε
        real k = epsilon * meanV*c * eta / dxP; // Permeability [m²] (1D ~ µm²) // εu ~ superficial fluid flow rate (m³/s)/m²)
        t++;
        permeability.insert(t, k*1e15);
        log(t, totalTime/(t-profileStartStep), "ms", "u="+str(meanV),"c",meanV*c*1e6,"μm/s", k*1e15,"mD", total?str("(Collide:", str(round(100.0*collide/total))+"%"_, "Stream:", str(round(100.0*stream/total))+"%"_, "Other:", str(round(100.0*other/total))+"%)"_):""_);
#if TUBE
        const uint R = 64;
        real meanV_tube = dxP/(8*eta)*sq(R*dx);
        real epsilon_tube = PI*R*R/(gridSize.x*gridSize.y);
        real k_tube = epsilon_tube * meanV_tube * eta / dxP; // Permeability [m²] (1D ~ µm²) // εu ~ superficial fluid flow rate (m³/s)/m²)
        log("Tube", meanV_tube*1e6,"μm/s", k_tube*1e15,"mD");
#endif
        other.start();
        window.render();
    }

#if SLICE // Slice
    Window window{this, int2(512), "Speed"_};

    float sliceZ = 0.5;
    bool mouseEvent(int2 cursor, int2 size, Event, Button) { sliceZ = clip(0.f, float(cursor.x)/size.x, 1.f); return false; }

    void render(int2 position, int2 size) {
        constexpr int upscale=1; assert_(upscale*gridSize.xy()==size);
        const int64 X=gridSize.x, Y=gridSize.y;
        int z = sliceZ * (gridSize.z-1);
        assert_(z>=0 && z<gridSize.z);
        for(int y: range(Y)) for(int x: range(X)) {
            real uz=0;
            uint offset = latticeLookup[z*Y*X+y*X+x];
            if(offset!=Solid) {
                const real* cell = source + offset*28;
                real rho = 0; real puz = 0;
                for(int i: range(3*3*3)) {
                    real wphi = w[i] * cell[i];
                    rho += wphi;
                    puz += wphi * vz[i];
                }
                uz = (dt/(2*c))*g.z + puz/rho;
            }
            if(uz>=0) {
                uint linear = clip(0,(int)round(0xFF*uz/meanV),0xFF);
                extern uint8 sRGB_lookup[256];
                uint sRGB = sRGB_lookup[linear];
                for(int dy: range(upscale)) for(int dx: range(upscale)) framebuffer((position.x+x)*upscale+dx,(position.y+y)*upscale+dy) = byte4(sRGB,sRGB,sRGB,0xFF);
            } else {
                uint linear = clip(0,(int)round(0xFF*(-uz)/meanV),0xFF);
                extern uint8 sRGB_lookup[256];
                uint sRGB = sRGB_lookup[linear];
                for(int dy: range(upscale)) for(int dx: range(upscale)) framebuffer((position.x+x)*upscale+dx,(position.y+y)*upscale+dy) = byte4(0,sRGB,0,0xFF);
            }
        }
        /*if(t<1024)*/ step();
    }
#else // Plot
    Window window{this, int2(640,480), "Permeability"_};

    void render(int2 position, int2 size) {
        Plot plot;
        plot.xlabel=String("t [step]"_), plot.ylabel=String("k [mD]"_);\
        plot.dataSets << move(permeability);
        plot.render(position, size);
        permeability = move(plot.dataSets.first());
        if(t<384) step();
    }
#endif

} test;
