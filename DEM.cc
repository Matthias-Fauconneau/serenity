#include "thread.h"
#include "window.h"
#include "time.h"
#include "matrix.h"
#include "render.h"
#include "algebra.h"
#include "png.h"
#include "time.h"
#include "parallel.h"
#include "gl.h"
#include "layout.h"
#include "plot.h"
#include "encoder.h"
FILE(shader)

struct System {
    static constexpr bool implicit = true; //  (m - δt²·∂xf - δt·∂vf) δv = δt·(f + δt·∂xf·v) else m δv = δt f
    static constexpr bool rollTest = false;
    static constexpr bool useWire = !rollTest;
    static constexpr bool useRotation = true;
    static constexpr int subStepCount = 16 * (rollTest?64:1);
    static constexpr float dt = 1./60 / subStepCount;
    // Characteristic dimensions
    static constexpr float T = 1; // ~ 1 s
    static constexpr float L = 1; // ~ 1 m
    static constexpr float M = 1; // M ~ ρL³ ~ 1000 kg

    HList<Plot> plots;

    struct Contact {
        vec3 relativeA, relativeB; // Relative to center but on world axis (not local rotation)
        vec3 normal;
        float depth;
    };

    /// Returns contact between two objects
    /// Defaults implementation for spheres
    template<Type tA, Type tB> Contact contact(const tA& A, size_t a, const tB& B, size_t b) {
        vec3 relativePosition = A.position[a] - B.position[b];
        float length = ::length(relativePosition);
        vec3 normal = relativePosition/length; // B -> A
        return {tA::radius*(-normal), tB::radius*(+normal),
                       normal, length-tA::radius-tB::radius};
    }

    struct Friction {
        size_t index; // Identifies contact to avoid duplicates and evaluate friction
        vec3 localA, localB; // in relative reference frames
        size_t lastUpdate = 0; // Time step of last update (to remove stale frictions)
        vec3 normal = 0;
        float fN = 0;
        vec3 relativeVelocity = 0;
#if DBG_FRICTION
        rgb3f color = 0; // DBG_FRICTION
        bool disable = false; //DBG_FRICTION
        array<vec3> lines = array<vec3>(); // DBG_FRICTION
#endif
        bool operator==(const Friction& b) const { return index == b.index; }
    };

    struct Floor {
        static constexpr float mass = inf;
        static constexpr float radius = inf;
        static constexpr float height = 8*L/256; //Wire::radius;
        static constexpr float thickness = L;
        static constexpr float curvature = 0;
        static constexpr float elasticModulus = 16 * M/(L*T*T);
        static constexpr float normalDamping = M / T;
        static constexpr float frictionCoefficient = 1;
        static constexpr float staticFrictionThresholdSpeed = /*Grain::radius*/L/32 / T;
        static constexpr size_t base = 0, capacity = 1;
        static constexpr bool fixed = true;
        static constexpr vec3 position[1] {vec3(0,0,0)};
        static constexpr vec3 velocity[1] {vec3(0,0,0)};
        static constexpr vec3 angularVelocity[1] {vec3(0,0,0)};
        static constexpr quat rotation[1] {quat{1,vec3(0,0,0)}};
        vec3 torque[0] {};
        vec3 surfaceVelocity(size_t, vec3) const { return 0; }
    } floor;
    /// Sphere - Floor
    template<Type tA> Contact contact(const tA& A, size_t a, const Floor&, size_t) {
        vec3 normal (0, 0, 1);
        return {tA::radius*(-normal), vec3(A.position[a].xy(), Floor::height), normal,
                    A.position[a].z-tA::radius-Floor::height};
    }

    struct Side {
        static constexpr float mass = inf;
        static constexpr float radius = inf;
        static constexpr float thickness = L;
        static constexpr float curvature = 0; // -1/radius?
        static constexpr float elasticModulus = 16 * M/(L*T*T);
        static constexpr float normalDamping = M / T;
        static constexpr float frictionCoefficient = 1;
        static constexpr float staticFrictionThresholdSpeed = /*Grain::radius*/L/32 / T;
        static constexpr bool fixed = true;
        static constexpr vec3 position[1] {vec3(0,0,0)};
        static constexpr vec3 velocity[1] {vec3(0,0,0)};
        static constexpr vec3 angularVelocity[1] {vec3(0,0,0)};
        static constexpr quat rotation[1] {quat{1,vec3(0,0,0)}};
        vec3 torque[0] {};
        static constexpr float initialRadius = L/2/2;
        float currentRadius = initialRadius;
        static constexpr size_t base = Floor::base+Floor::capacity, capacity = 1;
        vec3 surfaceVelocity(size_t, vec3) const { return 0; }
    } side;
    /// Sphere - Side
    template<Type tA> Contact contact(const tA& A, size_t a, const Side& side, size_t) {
        vec2 r = A.position[a].xy();
        float length = ::length(r);
        vec3 normal = vec3(-r/length, 0); // Side -> Sphere
        return {tA::radius*(-normal), vec3((side.currentRadius)*-normal.xy(), A.position[a].z),
                    normal, side.currentRadius-tA::radius-length};
    }

    struct Grain {
        // Properties
        static constexpr float radius = L/32; // 62 mm diameter
        static constexpr float thickness = radius;
        static constexpr float curvature = 1./radius;
        static constexpr float volume = 4./3*PI*cb(radius);
        static constexpr float density = M/cb(L);
        static constexpr float mass = density * volume;
        static constexpr float elasticModulus = 8 * M / (L*T*T);
        static constexpr float normalDamping = 32 * mass / T; // TODO: from restitution coefficient
        static constexpr float frictionCoefficient = 1;
        static constexpr float staticFrictionThresholdSpeed = Grain::radius / T;

        static constexpr size_t base = Side::base+Side::capacity;
        static constexpr size_t capacity = rollTest ? 1 : 512;
        static constexpr bool fixed = false;
        buffer<vec3> position { capacity };
        buffer<vec3> velocity { capacity };
        buffer<quat> rotation { capacity };
        buffer<vec3> angularVelocity { capacity };
        buffer<vec3> torque { capacity };
        buffer<array<Friction>> frictions { capacity };
        Grain() { frictions.clear(); }
        size_t count = 0;
    } grain;

    struct Wire {
        static constexpr float radius = L/256; // 1/256 = 8 mm diameter
        static constexpr float thickness = radius;
        static constexpr float curvature = 1./radius;
        static constexpr float internodeLength = 4*radius;
        static constexpr float volume = PI * sq(radius) * internodeLength;
        static constexpr float density = M / cb(L);
        static constexpr float mass = density * volume;
        static constexpr float elasticModulus = 1 * M / (L*T*T); // 8
        static constexpr float normalDamping = 128 * mass / T; // TODO: from restitution coefficient
        static constexpr float frictionCoefficient = 1;
        static constexpr float staticFrictionThresholdSpeed = inf; //2 * Grain::radius / T;

        static constexpr float tensionStiffness = 0x1p17 * mass / (T*T); // 16
        static constexpr float tensionDamping = 0 * mass/T; // 4

        static constexpr size_t base = Grain::base+Grain::capacity;
        static constexpr size_t capacity = useWire ? 1024 : 0;
        static constexpr bool fixed = false;
        buffer<vec3> position { capacity };
        buffer<vec3> velocity { capacity };
        buffer<quat> rotation { capacity }; // FIXME
        buffer<vec3> angularVelocity { capacity }; // FIXME
        buffer<vec3> torque { capacity }; // FIXME
        buffer<array<Friction>> frictions { capacity };
        Wire() { frictions.clear(); }
        size_t count = 0;
    } wire;

    // Update
    static constexpr size_t capacity = Wire::base+Wire::capacity;
    Matrix matrix { implicit ? capacity*3 : 0};
    buffer<vec3d> F { capacity };
    System() { F.clear(); }

    size_t timeStep = 0;
    bool stop = false;

    template<Type tA> vec3 toGlobal(tA& A, size_t a, vec3 localA) {
        return A.position[a] + (A.rotation[a] * quat{0, localA} * A.rotation[a].conjugate()).v;
    }

    /// Evaluates contact penalty between two objects
    template<Type tA, Type tB> void penalty(const tA& A, size_t a, const tB& B, size_t b) {
        Contact c = contact(A, a, B, b);
        if(c.depth >= 0) return;
        // Stiffness
        constexpr float E = 1/(1/tA::elasticModulus+1/tB::elasticModulus);
        constexpr float R = 1/(tA::curvature+tB::curvature);
        const float Ks = E*sqrt(R)*sqrt(-c.depth);
        // Damping
        constexpr float Kb = 1/(1/tA::normalDamping+1/tB::normalDamping);
        vec3 relativeVelocity =
                A.velocity[a] + cross(A.angularVelocity[a], c.relativeA) -
                B.velocity[b] + cross(B.angularVelocity[b], c.relativeB);
        float fN = spring<tA, tB>(a, b, Ks, c.depth, 0, Kb, c.normal, relativeVelocity);
        vec3 localA = (A.rotation[a].conjugate()*quat{0, c.relativeA}*A.rotation[a]).v;
        vec3 localB = (B.rotation[b].conjugate()*quat{0, c.relativeB}*B.rotation[b]).v;
        Friction& friction = A.frictions[a].add(Friction{tB::base+b, localA, localB});
        friction.lastUpdate = timeStep;
        friction.normal = c.normal;
        friction.fN = fN;
        friction.relativeVelocity = relativeVelocity;
    }
    template<Type tA, Type tB> float spring(size_t a, size_t b,
                                           float Ks, float length, float restLength, float Kb, vec3 normal, vec3 relativeVelocity) {
        float fS = - Ks * (length - restLength);
        float fB = - Kb * dot(normal, relativeVelocity); // Damping
        vec3 f = (fS + fB) * normal;
        if(implicit) {
            mat3 o = outer(normal, normal);
            assert_(length);
            mat3 dxf = - Ks * ((1 - restLength/length)*(1 - o) + o);
            mat3 dvf = - Kb * o;
            for(int i: range(3)) {
                for(int j: range(3)) {
                    if(!tA::fixed) matrix((tA::base+a)*3+i, (tA::base+a)*3+j) += - dt*dt*dxf(i, j) - dt*dvf(i, j);
                    if(!tB::fixed) matrix((tB::base+b)*3+i, (tB::base+b)*3+j) -= - dt*dt*dxf(i, j) - dt*dvf(i, j);
                }
                if(!tA::fixed) F[tA::base+a][i] += dt*f[i] + dt*dt*dxf(i, i) * relativeVelocity[i];
                if(!tB::fixed) F[tB::base+b][i] -= dt*f[i] + dt*dt*dxf(i, i) * relativeVelocity[i];
            }
        } else {
            if(!tA::fixed) F[tA::base+a] += vec3d(dt*f);
            if(!tB::fixed) F[tB::base+b] -= vec3d(dt*f);
        }
        return fS + fB;
    }
    template<Type tA, Type tB> bool friction(Friction& f, tA& A, size_t a, tB& B, size_t b) {
        if(f.lastUpdate != timeStep) return false;
        vec3 fT = 0;
        vec3 tangentRelativeVelocity = f.relativeVelocity - dot(f.normal, f.relativeVelocity) * f.normal;
        float tangentRelativeSpeed = ::length(tangentRelativeVelocity);
        constexpr float staticFrictionThresholdSpeed = ::max(tA::staticFrictionThresholdSpeed, tB::staticFrictionThresholdSpeed);
        bool staticFriction =  tangentRelativeSpeed < staticFrictionThresholdSpeed;
        constexpr float frictionCoefficient = 2/(1/tA::frictionCoefficient+1/tB::frictionCoefficient);
        vec3 relativeA = (A.rotation[a-tA::base] * quat{0, f.localA} * A.rotation[a-tA::base].conjugate()).v;
        vec3 relativeB = (B.rotation[b-tB::base] * quat{0, f.localB} * B.rotation[b-tB::base].conjugate()).v;
        vec3 globalA = A.position[a-tA::base] + relativeA;
        vec3 globalB = B.position[b-tB::base] +  relativeB;
        if(staticFriction) {
            vec3 x = globalA - globalB;
            float distance = ::length(x);
            constexpr float R = Grain::radius; //min(tA::radius, tB::radius);
            staticFriction = distance < R; //2;
            if(staticFriction) {
                vec3 tangentOffset = x - dot(f.normal, x) * f.normal;
                float tangentLength = ::length(tangentOffset);
                staticFriction = tangentLength < R; //16;
                if(staticFriction) {
                    if(tangentLength) {
                        const float staticFrictionStiffness =
                                max(frictionCoefficient / min(tA::radius, tB::radius),
                                1*Wire::elasticModulus*Wire::radius);
                        //log(1*Wire::elasticModulus*Wire::radius, frictionCoefficient / min(tA::radius, tB::radius));
                        float force = staticFrictionStiffness * f.fN * tangentLength;
                        vec3 springDirection = tangentOffset / tangentLength;
                        //log(x/Wire::radius, grain.velocity[0]/(Wire::radius*dt), grain.angularVelocity[0]);
                        //force = min(force, min(tA::mass, tB::mass)*tangentLength/(T*T)); // Restores in T
                        fT -= force * springDirection; // FIXME: implicit zero length spring ?
                    } // else static equilibrium
                    constexpr float tangentialDamping = dt / T * min(tA::mass, tB::mass) / T;
                    //fT -= tangentialDamping * dot(springDirection, tangentRelativeVelocity) * springDirection;
                    fT -= tangentialDamping * tangentRelativeVelocity;
#if DBG_FRICTION
                    f.color = rgb3f(0,1,0);
#endif
                }
            }
        }
        // Dynamic friction
        //if(/*!staticFriction &&*/ tangentRelativeSpeed /*>= staticFrictionThresholdSpeed*/) {
        if(tangentRelativeSpeed >= staticFrictionThresholdSpeed /*|| length(fT) > frictionCoefficient*f.fN*/) {
            staticFriction = false;
            constexpr float dynamicFrictionCoefficient = frictionCoefficient / 64;
            fT = - dynamicFrictionCoefficient * f.fN * tangentRelativeVelocity
                    / (tangentRelativeSpeed+staticFrictionThresholdSpeed);
#if DBG_FRICTION
            f.color = rgb3f(1,0,0);
#endif
        }
        if(!tA::fixed) F[a] += vec3d(dt*fT);
        if(!tB::fixed) F[b] -= vec3d(dt*fT);
        if(!tA::fixed) A.torque[a-tA::base] += cross(relativeA, fT);
        if(!tB::fixed) B.torque[b-tB::base] -= cross(relativeB, fT);
#if DBG_FRICTION
        f.lines.append(globalA);
        f.lines.append(globalA + fT);
#endif
        return staticFriction;
    }
};
 constexpr vec3 System::Floor::position[1];
 constexpr quat System::Floor::rotation[1];
 constexpr vec3 System::Floor::velocity[1];
 constexpr vec3 System::Floor::angularVelocity[1];
 constexpr vec3 System::Side::position[1];
 constexpr quat System::Side::rotation[1];
 constexpr vec3 System::Side::velocity[1];
 constexpr vec3 System::Side::angularVelocity[1];

// Grid

struct Grid : buffer<uint16> {
    static constexpr size_t cellCapacity = 64; // 2 cell / line
    int3 size;
    Grid(int3 size) : buffer(size.z*size.y*size.x*cellCapacity), size(size) { clear(); }
    struct List : mref<uint16> {
        List(mref<uint16> o) : mref(o) {}
        void remove(uint16 index) {
            size_t i = 0;
            while(at(i)) { if(at(i)==index) break; i++; }
            assert(i<cellCapacity);
            while(i+1<cellCapacity) { at(i) = at(i+1); i++; }
            assert(i<cellCapacity);
            at(i) = 0;
        }
        void append(uint16 index) { // Sorted by decreasing index
            size_t i = 0;
            while(at(i) > index) { i++; assert(i<cellCapacity, (mref<uint16>)*this, index); }
            size_t j=i;
            while(index) { assert(j<cellCapacity, (mref<uint16>)*this, index); swap(index, at(j)); j++; }
            if(j<cellCapacity) at(j) = 0;
        }
    };
    List operator[](size_t i) { return slice(i*cellCapacity, cellCapacity); }
    size_t index(int x, int y, int z) {
        return (z*size[1]+y)*size[0]+x;
    }
    int3 index3(vec3 p) { // [-1..1, 0..2] -> [1..size-1]
        return int3(vec3(size)/2.f * (vec3(1,1,0)+p));
    }
    size_t index(vec3 p) {
        int3 i = index3(p);
        assert_(i.x == clamp(0, i.x, size.x-1), p, i, size);
        assert_(i.y == clamp(0, i.y, size.y-1), p, i, size);
        assert_(i.z == clamp(0, i.z, size.z-1), p, i, size);
        return index(i.x, i.y, i.z);
    }
};

struct Simulation : System {
    // Space partition
    Grid grainGrid {32/*2/diameter*/}, wireGrid {256/*2/diameter*/};

    template<Type T> void update(const ref<vec3d>& dv, Grid& grid, T& t, size_t i) {
        t.velocity[i] += vec3(dv[t.base+i]);
        size_t oldCell = grid.index(t.position[i]);
        t.position[i] += dt*t.velocity[i];
        if(t.position[i].x != clamp(-1.f, t.position[i].x, 1.f-0x1p-12f) ||
                t.position[i].y != clamp(-1.f, t.position[i].y, 1.f-0x1p-12f) ||
                t.position[i].z != clamp(0.f, t.position[i].z, 2.f-0x1p-12f)) {
            stop = true;
            log(i, "p", t.position[i], "v", t.velocity[i], "dv", dv[t.base+i], "F", F[t.base+i]);
            t.position[i].x = clamp(-1.f, t.position[i].x, 1.f-0x1p-12f);
            t.position[i].y = clamp(-1.f, t.position[i].y, 1.f-0x1p-12f);
            t.position[i].z = clamp(0.f, t.position[i].z, 2.f-0x1p-12f);
            return;
        }
        size_t newCell = grid.index(t.position[i]);
        if(oldCell != newCell) {
            grid[oldCell].remove(1+i);
            grid[newCell].append(1+i);
        }
    }

    // Process
    static constexpr float winchRadius = Side::initialRadius - Grain::radius;
    static constexpr float pourRadius = Side::initialRadius - Grain::radius;
    static constexpr float winchRate = 16 / T, winchSpeed = 2 * Grain::radius / T;
    float winchAngle = 0, pourHeight = Floor::height+Grain::radius;
    Random random;
    bool pour = true;

    // Performance
    int64 lastReport = realTime();
    Time totalTime {true}, stepTime;
    Time processTime, miscTime, grainTime, wireTime, solveTime;
    Time grainContactTime, grainFrictionTime, grainIntegrationTime;
    Time wireContactTime, wireFrictionTime, wireIntegrationTime;

    // DBG_FRICTION
    Lock lock;
    array<uint16> fixed;

    Simulation() {
        F[0] = 0;
        if(useWire) { // Winch obstacle
            size_t i = grain.count; grain.count++;
            vec3 winchPosition (winchRadius*cos(winchAngle), winchRadius*sin(winchAngle),
                                pourHeight+Grain::radius);
            grain.position[i] = vec3(winchPosition.xy(), winchPosition.z+Grain::radius-Wire::radius);
            grain.velocity[i] = 0;
            grain.frictions.set(i);
            grainGrid[grainGrid.index(grain.position[i])].append(1+i);
            grain.rotation[i] = quat();
            grain.angularVelocity[i] = 0;
        }
    }

    // Single implicit Euler time step
    void step() {
        stepTime.start();
        processTime.start();
        // Process
        if(pour && (pourHeight>=2 || grain.count == grain.capacity
                    || (wire.capacity && wire.count == wire.capacity))) {
            log("Release", "Grain", grain.count, "Wire", wire.count);
            pour = false;
            /*for(size_t i: range(grain.count)) {
                if(grain.position[i].z <= Floor::height+Grain::radius) // Floor contact
                    fixed.append(Grain::base+i); // Fix position (no floor roll after release)
            }*/
        }
        if(!pour && side.currentRadius < L-Grain::radius) {
            side.currentRadius += L / T * dt;
        }
        if(pour) {
            // Generates falling grain (pour)
            if(1) for(;;) {
                vec3 p(random()*2-1,random()*2-1, pourHeight);
                //vec3 p(0,0, grain.count ? 2-grain.radius : grain.radius);
                if(length(p.xy())>1) continue;
                vec3 newPosition (pourRadius*p.xy(), p.z); // Within cylinder
                for(vec3 p: wire.position.slice(0, wire.count))
                    if(length(p - newPosition) < Grain::radius+Wire::radius) goto break2_;
                for(vec3 p: grain.position.slice(0, grain.count))
                    if(length(p - newPosition) < Grain::radius+Grain::radius) goto break2_;
                size_t i = grain.count; grain.count++;
                grain.position[i] = newPosition;
                grain.velocity[i] = 0;
                grain.frictions.set(i);
                grainGrid[grainGrid.index(grain.position[i])].append(1+i);
                float t0 = 2*PI*random();
                float t1 = acos(1-2*random());
                float t2 = (PI*random()+acos(random()))/2;
                grain.rotation[i] = {cos(t2), vec3(sin(t0)*sin(t1)*sin(t2), cos(t0)*sin(t1)*sin(t2), cos(t1)*sin(t2))};
                //grain.rotation[i] = quat{1, 0};
                grain.angularVelocity[i] = 0;
                if(grain.capacity == 1 && i == 0) { // Push single particle to center
                    //grain.velocity[0]  =  vec3((- grain.position[0] / T).xy(), 0);
                    grain.angularVelocity[i] = vec3(0,0,1);
                }
                else if(grain.capacity == 2 && i == 1) { // Push second particle to the first
                    grain.velocity[1] = (grain.position[0] - grain.position[1]) / T;
                }
                break;
            }
            break2_:;
            pourHeight += winchSpeed * dt;
            if(useWire) { // Generates wire (winch)
                winchAngle += winchRate * dt;
                vec3 winchPosition (winchRadius*cos(winchAngle), winchRadius*sin(winchAngle),
                                    pourHeight+Grain::radius);

                // Moves winch obstacle keeping spawn clear
                size_t i = 0;
                size_t oldCell = grainGrid.index(grain.position[i]);
                grain.position[i] = vec3(winchPosition.xy(), winchPosition.z+Grain::radius-Wire::radius);
                grain.velocity[i] = 0;
                grain.angularVelocity[0] = 0;
                size_t newCell = grainGrid.index(grain.position[i]);
                if(oldCell != newCell) {
                    grainGrid[oldCell].remove(1+i);
                    grainGrid[newCell].append(1+i);
                }
                vec3 lastPosition = wire.count ? wire.position[wire.count-1] : vec3(winchRadius, 0, pourHeight);
                vec3 r = winchPosition - lastPosition;
                float l = length(r);
                if(l > Wire::internodeLength*(1+1./4)) {
                    size_t i = wire.count; wire.count++;
                    vec3 pos = lastPosition + Wire::internodeLength/l*r;
                    wire.position[i] = pos;
                    wire.rotation[i] = quat();
                    wire.velocity[i] = 0;
                    wire.angularVelocity[i] = 0;
                    wire.frictions.set(i);
                    wireGrid[wireGrid.index(wire.position[i])].append(1+i);
                }
            }
        } else if(useWire) {
            vec3 winchPosition (winchRadius*cos(winchAngle), winchRadius*sin(winchAngle),
                                pourHeight+Grain::radius);
            size_t i = 0;
            grain.position[i] = vec3(winchPosition.xy(), winchPosition.z+Grain::radius-Wire::radius);
            grain.velocity[i] = 0;
            grain.angularVelocity[i] = 0;
        }
        processTime.stop();

        // Initialization
        miscTime.start();
        matrix.clear();
        miscTime.stop();
        constexpr vec3 g {0, 0, -1};

        // Grain
        {grainTime.start();
            // Initialization
#if DBG_FRICTION
            Locker lock(this->lock);
            for(size_t i: range(grain.count)) {
                //DBG_FRICTION: kept from previous step for visualization
                size_t a = i;
                for(size_t i=0; i<grain.frictions[a].size;) { // TODO: keep non-trailing slots to avoid moves
                    Friction& f = grain.frictions[a][i];
                    if(f.disable) { grain.frictions[a].removeAt(i); continue; }
                    else i++;
                }
            }
#endif
            for(size_t i: range(grain.count)) {
                grain.torque[i] = 0;
                if(implicit) for(int c: range(3)) matrix((grain.base+i)*3+c, (grain.base+i)*3+c) = Grain::mass;
                F[grain.base+i] = vec3d(dt * Grain::mass * g);
                penalty(grain, i, floor, 0);
                penalty(grain, i, side, 0);
            }
            // Grain - Grain frictions
            grainContactTime.start();
            parallel_for(grain.count, [this](uint, int a) {
                int3 index = grainGrid.index3(grain.position[a]);
                for(int z: range(max(0, index.z-1), min(index.z+2, grainGrid.size.z))) {
                    for(int y: range(max(0, index.y-1), min(index.y+2, grainGrid.size.y))) {
                        for(int x: range(max(0, index.x-1), min(index.x+2, grainGrid.size.x))) {
                            Grid::List list = grainGrid[grainGrid.index(x, y, z)];
                            for(size_t i=0; i<grainGrid.cellCapacity; i++) {
                                int b = list[i]-1;
                                if(!(a < b)) break; // Single penalty per pair, until 0
                                penalty(grain, a, grain, b);
                            }
                        }
                    }
                }
            });
            grainContactTime.stop();

            // Grain - Grain friction
            grainFrictionTime.start();
            parallel_for(grain.count, [this](uint, size_t a) {
                for(size_t i=0; i<grain.frictions[a].size;) {
                    Friction& f = grain.frictions[a][i];
                    size_t b = f.index; // index 0 is bound (floor or side)
                    //if(b>1) { f.disable = true; i++; continue; } // DBG_FRICTION: no GG
                    bool staticFriction;
                    if(b==0) staticFriction = friction(f, grain, Grain::base+a, floor, b);
                    else if(b==1) staticFriction = friction(f, grain, Grain::base+a, side, b);
                    else staticFriction = friction(f, grain, Grain::base+a, grain, b);
                    if(staticFriction) i++;
#if DBG_FRICTION
                    else { f.disable = true; i++; } // DBG_FRICTION
#else
                    else { grain.frictions[a].removeAt(i); continue; } // No more contact or dynamic friction
#endif
                }
            });
            grainFrictionTime.stop();
        grainTime.stop();}

        // Wire
        // Initialization
        {wireTime.start();
#if DBG_FRICTION
            Locker lock(this->lock);
            for(size_t i: range(wire.count)) {
                {//DBG_FRICTION: kept from previous step for visualization
                    size_t a = i;
                    for(size_t i=0; i<wire.frictions[a].size;) { // TODO: keep non-trailing slots to avoid moves
                        Friction& f = wire.frictions[a][i];
                        if(f.disable) { wire.frictions[a].removeAt(i); continue; }
                        else i++;
                    }
                }
            }
#endif

            for(size_t i: range(wire.count)) {
                if(implicit) for(int c: range(3)) matrix((wire.base+i)*3+c, (wire.base+i)*3+c) = Wire::mass;
                F[wire.base+i] = vec3d(dt * Wire::mass * g);
                penalty(wire, i, floor, 0);
                penalty(wire, i, side, 0);
            }

            // Tension
            for(size_t i: range(1, wire.count)) {
                size_t a = i-1, b = i;
                vec3 relativePosition = wire.position[a] - wire.position[b];
                float length = ::length(relativePosition);
                //if(!length) continue; // FIXME
                assert_(length, "tension", i, wire.position[a], wire.position[b]);
                float restLength = Wire::internodeLength;
                vec3 normal = relativePosition/length;
                vec3 relativeVelocity = wire.velocity[a] - wire.velocity[b];
                spring<Wire, Wire>(a, b, Wire::tensionStiffness, length, restLength, Wire::tensionDamping, normal,
                                   relativeVelocity);
            }

            wireContactTime.start();
            parallel_for(wire.count, [this](uint, int a) {
                /*{ // Wire - Wire
                int3 index = wireGrid.index3(position[a]);
                for(int z: range(max(0, index.z-1), min(index.z+2, wireGrid.size.z))) {
                    for(int y: range(max(0, index.y-1), min(index.y+2, wireGrid.size.y))) {
                        for(int x: range(max(0, index.x-1), min(index.x+2, wireGrid.size.x))) {
                            Grid::List list = wireGrid[wireGrid.index(x, y, z)];
                            for(size_t i=0; i<wireGrid.cellCapacity; i++) {
                                int b = list[i]-1;
                                if(!(a+1 < b)) break; // No adjacent, single contact per pair, until 0
                                penalty(wire, a, wire, b);
                            }
                        }
                    }
                }
            }*/
                { // Wire - Grain
                    int3 index = grainGrid.index3(wire.position[a]);
                    for(int z: range(max(0, index.z-1), min(index.z+2, grainGrid.size.z))) {
                        for(int y: range(max(0, index.y-1), min(index.y+2, grainGrid.size.y))) {
                            for(int x: range(max(0, index.x-1), min(index.x+2, grainGrid.size.x))) {
                                Grid::List list = grainGrid[grainGrid.index(x, y, z)];
                                for(size_t i=0; i<grainGrid.cellCapacity; i++) {
                                    int b = list[i]-1;
                                    if(b<=0) break; // until 0, except grain #0 (winch obstacle)
                                    penalty(wire, a, grain, b);
                                }
                            }
                        }
                    }
                }
            });
            wireContactTime.stop();

            // Wire - Grain friction
            wireFrictionTime.start();
            parallel_for(wire.count, [this](uint, size_t a) {
                for(size_t i=0; i<wire.frictions[a].size;) {
                    Friction& f = wire.frictions[a][i];
                    //if(f.lastUpdate != timeStep) { wire.frictions[a].removeAt(i); continue; } else i++;
                    size_t b = f.index;
                    bool staticFriction;
                    if(b==0) staticFriction = friction(f, wire, Wire::base+a, floor, b);
                    else if(b==1) staticFriction = friction(f, wire, Wire::base+a, side, b);
                    else staticFriction = friction(f, wire, Wire::base+a, grain, b);
                    if(staticFriction) i++;
#if DBG_FRICTION
                    else { f.disable = true; i++; } // DBG_FRICTION
#else
                    else { wire.frictions[a].removeAt(i); continue; } // No more contact or dynamic friction
#endif
                }
            });
            wireFrictionTime.stop();
        wireTime.stop();}

        solveTime.start();
        buffer<vec3d> dv;
        if(implicit) dv = cast<vec3d>(UMFPACK(matrix).solve(cast<real>(F)));
        else {
            dv = buffer<vec3d>(F.capacity);
            for(size_t i: range(grain.count)) dv[grain.base+i] = F[grain.base+i] / real(Grain::mass);
            for(size_t i: range(wire.count)) dv[wire.base+i] = F[wire.base+i] / real(Wire::mass);
        }
        solveTime.stop();

        grainTime.start();
        grainIntegrationTime.start();
        for(size_t i: range(grain.count)) update(dv, grainGrid, grain, i);
        if(useRotation) parallel_for(grain.count, [this](uint, size_t i) {
            static constexpr float I (2./3*Grain::mass*sq(Grain::radius)); // mat3
#if 1 // PCDM rotation integration
            quat& q = grain.rotation[i];
            vec3& ww = grain.angularVelocity[i];
            vec3 w = (q.conjugate() * quat{ww?0.f:1, ww} * q).v; // Local
            vec3 t = (q.conjugate() * quat{grain.torque[i]?0.f:1, grain.torque[i]} * q).v; // Local
            vec3 dw = dt/I * (t - cross(w, I*w));
            vec3 w4 = w + 1.f/4*dw;
            vec3 w4w = (q * quat{0, w4} * q.conjugate()).v; // Maps back to world vector
            float w4l = length(w4w);
            // Prediction (multiplicative update)
            quat qp = w4l ? quat{cos(dt/4*w4l), sin(dt/4*w4l)*(w4w/w4l)} * q : q; // Rotates q by dt/4*w4w
            vec3 w2 = w + 1.f/2*dw;
            vec3 w2w = (qp * quat{0, w2} * qp.conjugate()).v; // Maps back to world vector using prediction
            float w2wl = length(w2w);
            // Correction (multiplicative update)
            // Rotates q by dt/4*w2w
            q = w2wl ? quat{cos(dt/2*w2wl), sin(dt/2*w2wl)*(w2w/w2wl)} * q : q;
            q = normalize(q); // FIXME
            ww = (q * quat{0, w + dw} * q.conjugate()).v;
#else // Euler
            vec3 w = grain.angularVelocity[i];
            grain.rotation[i] = normalize(grain.rotation[i] + dt/2 * grain.rotation[i] * quat{0, w});
            grain.angularVelocity[i] += dt/I * (grain.torque[i] - cross(w, I*w));
#endif
        });
        grainIntegrationTime.stop();
        grainTime.stop();

        wireTime.start();
        wireIntegrationTime.start();
        for(size_t i: range(wire.count)) update(dv, wireGrid, wire, i);
        wireIntegrationTime.stop();
        wireTime.stop();

        timeStep++;

        if(wire.count) {
            miscTime.start();
            float wireLength = 0;
            vec3 last = wire.position[0];
            for(vec3 p: wire.position.slice(1, wire.count-1)) {
                wireLength += ::length(p-last);
                last = p;
            }
            assert_(isNumber(wireLength), wireLength);
            if(!plots) plots.append();
            //plots[0].dataSets["length"__][timeStep] = wireLength;
            /*Locker lock(this->lock);
            plots[0].dataSets["stretch"__][timeStep] = (wireLength / wire.count) / Wire::internodeLength;*/
            miscTime.stop();
        }
        stepTime.stop();
    }
};

struct SimulationView : Simulation, Widget, Poll {
    Time renderTime;

    void step() {
        Simulation::step();
        if(timeStep%subStepCount == 0) window->render();
        if(realTime() > lastReport+2e9) {
            log("step",str(stepTime, totalTime),
                //"render", str(renderTime, totalTime), "GPU", str(window->swapTime, totalTime),
                //"process",str(processTime, stepTime),
                "misc",str(miscTime, stepTime),
                "grain",str(grainTime, stepTime),
                "wire",str(wireContactTime, stepTime),
                "solve",str(solveTime, stepTime),
                "grainContact",str(grainContactTime, grainTime), "grainFriction",str(grainFrictionTime, grainTime),
                "grainIntegration",str(grainIntegrationTime, grainTime),
                "wireContact",str(wireContactTime, wireTime)
                //"wireFriction",str(wireFrictionTime, wireTime),
                //"wireIntegration",str(wireIntegrationTime, wireTime)
                );
            lastReport = realTime();
#if PROFILE
            requestTermination();
#endif
        }
        if(!Simulation::stop) queue();
    }
    void event() {
#if PROFILE
        static unused bool once = ({ extern void profile_reset(); profile_reset(); true; });
#endif
        step();
    }

    unique<Window> window = ::window(this, -1);
    //Thread simulationThread;
    unique<Encoder> encoder = nullptr;

    SimulationView() /*: Poll(0, POLLIN, simulationThread)*/ {
        window->actions[Space] = [this]{
            writeFile(str(timeStep*dt)+".png", encodePNG(window->readback()), home()); };
        window->actions[Return] = [this]{
            if(pour) {
                pour = false; window->setTitle("Released");
                log("Release", "Grain", grain.count, "Wire", wire.count);
            }
        };
        window->actions[Escape] = [this]{
            encoder = nullptr;
            log("Grain", grain.count, "Wire", wire.count);
            requestTermination();
        };
        window->actions[Key('E')] = [this] {
            if(!encoder) {
                encoder = unique<Encoder>("tas.mp4");
                encoder->setH264(window->Window::size, 60);
                encoder->open();
            }
        };
        if(arguments().contains("export")) {
            encoder = unique<Encoder>("tas.mp4"_);
            encoder->setH264(window->Window::size, 60);
            encoder->open();
        }

        //simulationThread.spawn();
        queue();
    }
    vec2 sizeHint(vec2) { return arguments().contains("export") ? vec2(1050) : 1050; }
    shared<Graphics> graphics(vec2 size) {
        renderTime.start();
        glDepthTest(true);
        mat4 viewProjection = mat4() .scale(vec3(2, 2*size.x/size.y, -1))
                .rotateX(viewYawPitch.y) .rotateZ(viewYawPitch.x)
                .translate(vec3(0,0, -1./4));
        quat viewRotation =
                quat{cos(viewYawPitch.y/2), sin(viewYawPitch.y/2)*vec3(1,0,0)} *
                quat{cos(viewYawPitch.x/2), sin(viewYawPitch.x/2)*vec3(0,0,1)};

        map<rgb3f, array<vec3>> lines;

        size_t start = pour || rollTest ? 0 : 1;
        if(grain.count-start) {
            const size_t grainCount = grain.count-start;
            buffer<float3> positions {grainCount*6};
#if DBG_FRICTION
            Locker lock(this->lock);
#endif
            for(size_t i: range(grainCount)) {
                // FIXME: GPU quad projection
                float3 O (viewProjection*grain.position[start+i]);
                float3 min = O - float3(float2(2*Grain::radius), 0); // Isometric
                float3 max = O + float3(float2(2*Grain::radius), 0); // Isometric
                positions[i*6+0] = min;
                positions[i*6+1] = float3(max.x, min.y, O.z);
                positions[i*6+2] = float3(min.x, max.y, O.z);
                positions[i*6+3] = float3(min.x, max.y, O.z);
                positions[i*6+4] = float3(max.x, min.y, O.z);
                positions[i*6+5] = max;

#if DBG_FRICTION
                if(0) {
                    lines[rgb3f(1,0,0)].append(viewProjection*toGlobal(grain, i, vec3(0,0,0)));
                    lines[rgb3f(1,0,0)].append(viewProjection*toGlobal(grain, i, vec3(Grain::radius/2,0,0)));
                    lines[rgb3f(0,1,0)].append(viewProjection*toGlobal(grain, i, vec3(0,0,0)));
                    lines[rgb3f(0,1,0)].append(viewProjection*toGlobal(grain, i, vec3(0,Grain::radius/2,0)));
                    lines[rgb3f(0,0,1)].append(viewProjection*toGlobal(grain, i, vec3(0,0,0)));
                    lines[rgb3f(0,0,1)].append(viewProjection*toGlobal(grain, i, vec3(0,0,Grain::radius/2)));
                }
                for(const Friction& f : grain.frictions[i]) {
                    vec3 A = toGlobal(grain, i, f.localA);
                    size_t b = f.index;
                    vec3 B;
                           if(b==0) B = toGlobal(floor, b-Floor::base, f.localB);
                    else if(b==1) B = toGlobal(side, b-Side::base, f.localB);
                    else B = toGlobal(grain, b-Grain::base, f.localB);
                    lines[f.color].append(viewProjection*A);
                    lines[f.color].append(viewProjection*B);
                }
#endif
            }

            static GLShader shader {::shader(), {"sphere"}};
            shader.bind();
            shader.bindFragments({"color"});
            static GLVertexArray vertexArray;
            GLBuffer positionBuffer (positions);
            vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
            GLBuffer rotationBuffer (apply(grain.rotation.slice(start),
                                           [=](quat q) -> quat { return q*viewRotation.conjugate(); }));
            shader.bind("rotationBuffer"_, rotationBuffer);
            vertexArray.draw(Triangles, positions.size);
        }

        if(wire.count>1) {
            const size_t wireCount = this->wire.count;
            buffer<float3> positions {(wireCount-1)*6};
#if DBG_FRICTION
            Locker lock(this->lock);
#endif
            for(size_t i: range(wireCount-1)) {
                vec3 a = wire.position[i], b = wire.position[i+1];
                // FIXME: GPU quad projection
                float3 A (viewProjection*a), B (viewProjection*b);
                float2 r = B.xy()-A.xy();
                float l = length(r);
                float2 t = r/l;
                float3 n (t.y, -t.x, 0);
                float width = 2*Wire::radius;
                float3 P[4] {A-width*n, A+width*n, B-width*n, B+width*n};
                positions[i*6+0] = P[0];
                positions[i*6+1] = P[1];
                positions[i*6+2] = P[2];
                positions[i*6+3] = P[2];
                positions[i*6+4] = P[1];
                positions[i*6+5] = P[3];

#if DBG_FRICTION
                for(const Friction& f : wire.frictions[i]) {
                    vec3 A = toGlobal(wire, i, f.localA);
                    size_t b = f.index;
                    vec3 B;
                    /**/  if(b==0) B = toGlobal(floor, b-Floor::base, f.localB);
                    else if(b==1) B = toGlobal(side, b-Side::base, f.localB);
                    else B = toGlobal(grain, b-Grain::base, f.localB);
                    lines[f.color].append(viewProjection*A);
                    lines[f.color].append(viewProjection*B);
                }
#endif
            }
            static GLShader shader {::shader(), {"cylinder"}};
            shader.bind();
            shader.bindFragments({"color"});
            static GLVertexArray vertexArray;
            GLBuffer positionBuffer (positions);
            vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
            vertexArray.draw(Triangles, positions.size);
        }

        //glDepthTest(false);
        static GLShader shader {::shader(), {"flat"}};
        shader.bind();
        shader.bindFragments({"color"});
        static GLVertexArray vertexArray;
        for(auto entry: lines) {
            shader["uColor"] = entry.key;
            GLBuffer positionBuffer (entry.value);
            vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
            vertexArray.draw(Lines, entry.value.size);
        }

        /*if(plots) {
            Image target(int2(size)/2, true); target.clear(byte4(byte3(0xFF), 0));
            this->lock.lock();
            auto graphics = this->plots.graphics(vec2(target.size), Rect(vec2(target.size)));
            this->lock.unlock();
            render(target, graphics);
            GLTexture image = flip(move(target));
            static GLShader shader {::shader(), {"blit"}};
            shader.bind();
            shader.bindFragments({"color"});
            static GLVertexArray vertexArray;
            shader["image"] = 0; image.bind(0);
            GLBuffer positionBuffer (ref<vec2>{vec2(0,0),vec2(1,0),vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1)});
            vertexArray.bindAttribute(shader.attribLocation("position"_), 2, Float, positionBuffer);
            glBlendAlpha();
            vertexArray.draw(Triangles, 6);
        }*/

        if(encoder) encoder->writeVideoFrame(window->readback());
        renderTime.stop();
        return shared<Graphics>();
    }

    // View
    vec2 lastPos; // Last cursor position to compute relative mouse movements
    vec2 viewYawPitch = vec2(0, 0/*-PI/3*/); // Current view angles (yaw,pitch)
    // Orbital ("turntable") view control
    bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        vec2 delta = cursor-lastPos; lastPos=cursor;
        if(event==Motion && button==LeftButton) {
            viewYawPitch += float(2.f*PI) * delta / size; //TODO: warp
            viewYawPitch.y= clamp(float(-PI), viewYawPitch.y, 0.f); // Keep pitch between [-PI, 0]
        }
        else return false;
        return true;
    }
} view;
