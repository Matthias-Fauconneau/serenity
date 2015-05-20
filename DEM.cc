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
FILE(shader)

struct System {
    struct Contact {
        size_t index; // Identifies contact to avoid duplicates and evaluate friction
        vec3 initialPosition; // Initial contact position
        size_t lastUpdate = 0; // Time step of last update (to remove stale contacts)
        vec3 normal = 0;
        real fN = 0;
        vec3 relativeVelocity = 0;
        vec3 currentPosition = 0;
        vec3 friction(const real frictionCoefficient) {
            vec3 fT = 0;
            const real dynamicFrictionCoefficient = frictionCoefficient; // [1]
            vec3 tangentRelativeVelocity = relativeVelocity - dot(normal, relativeVelocity) * normal;
            real tangentRelativeSpeed = ::length(tangentRelativeVelocity);
            if(tangentRelativeSpeed) fT -= dynamicFrictionCoefficient * fN * tangentRelativeVelocity / tangentRelativeSpeed;
            constexpr real maximumStaticFrictionTangentRelativeSpeed = Wire::radius / dt; // ~ wire radius / dt [L/T]
            static_assert(maximumStaticFrictionTangentRelativeSpeed == 1, "");
            if(tangentRelativeSpeed < maximumStaticFrictionTangentRelativeSpeed) { // + Static friction
                vec3 x = currentPosition - initialPosition;
                vec3 tangentOffset = x - dot(normal, x) * normal;
                real length = ::length(tangentOffset);
                constexpr real maximumStaticFrictionSpringLength = Wire::radius; // ~ wire radius [L]
                static_assert(maximumStaticFrictionSpringLength == 1./256, "");
                if(length < maximumStaticFrictionSpringLength) { // FIXME
                    const real staticFrictionStiffness = frictionCoefficient / 256 / Wire::radius; // [1/L]
                    assert(staticFrictionStiffness == 1);
                    fT -= staticFrictionStiffness * fN * tangentOffset; // FIXME: implicit zero length spring ?
                    if(length) {
                        vec3 springDirection = tangentOffset / length;
                        constexpr real tangentialDamping = 0x1p-20; // [F/V=M/T] FIXME
                        fT -= tangentialDamping * dot(springDirection, relativeVelocity) * springDirection;
                    } // else static equilibrium
                }
            } else initialPosition = currentPosition; // Dynamic friction only
            return fT;
        }
        bool operator==(const Contact& b) const { return index == b.index; }
    };

    template<Type A, Type B> struct pair { A a; B b; };
    /// Returns closest center points between two objects
    template<Type tA, Type tB> pair<vec3, vec3> closest(const tA& A, size_t a, const tB& B, size_t b) {
        return {A.position[a], B.position[b]};
    }

    struct Floor {
        static constexpr real mass = inf;
        static constexpr real thickness = 8;
        static constexpr real curvature = 0;
        static constexpr real normalStiffness = 1;
        static constexpr real normalDamping = 1;
        static constexpr size_t base = 0, capacity = 0; static constexpr vec3 velocity[0] = {};
        vec3 surfaceVelocity(size_t, vec3) const { return 0; }
    } floor;
    /// Floor - Sphere
    template<Type tA> pair<vec3, vec3> closest(const tA& A, size_t a, const Floor& B, size_t) {
        return {A.position[a], vec3(A.position[a].xy(), -B.thickness)};
    }

    struct Side {
        static constexpr real mass = inf;
        static constexpr real thickness = 8;
        static constexpr real curvature = 0; // -1/radius?
        static constexpr real normalStiffness = 1;
        static constexpr real normalDamping = 1;
        static constexpr real initialRadius = 1./2;
        real currentRadius = initialRadius;
        static constexpr size_t base = 0, capacity = 0; static constexpr vec3 velocity[0] = {};
        vec3 surfaceVelocity(size_t, vec3) const { return 0; }
    } side;
    /// Side - Sphere
    template<Type tA> pair<vec3, vec3> closest(const tA& A, size_t a, const Side& B, size_t) {
        vec2 r = A.position[a].xy();
        return {A.position[a], vec3((B.currentRadius+B.thickness)/length(r)*r, A.position[a].z)};
    }

    struct Grain {
        // Properties
        static constexpr real radius = 1./32; // 62 mm diameter
        static constexpr real thickness = radius;
        static constexpr real curvature = 1./radius;
        static constexpr real mass = 4./3*PI*cb(radius);
        static constexpr real normalStiffness = 1; // 64
        static constexpr real normalDamping = 1./128; // 64
        static constexpr real friction = 1;

        static constexpr size_t base = 0;
        static constexpr size_t capacity = 1024;
        buffer<vec3> position { capacity };
        buffer<vec3> velocity { capacity };
        buffer<array<Contact>> contacts { capacity };
        buffer<quat> rotation { capacity };
        buffer<vec3> angularVelocity { capacity };
        buffer<vec3> torque { capacity };
        size_t count = 0;

        vec3 surfaceVelocity(size_t i, vec3 n) const { return velocity[i] /*+ cross(angularVelocity[i], radius*n)*/; }
    } grain;

    struct Wire {
        static constexpr real radius = 1./256; // 1/256 = 8 mm diameter
        static constexpr real thickness = radius;
        static constexpr real curvature = 1./radius;
        static constexpr real internodeLength = 4*radius; // 4
        static constexpr real mass = PI * sq(radius) * internodeLength;
        static constexpr real normalStiffness = 1;
        static constexpr real normalDamping = 0x1p-16; // 16
        static constexpr real friction = 1;

        static constexpr real tensionStiffness = 1 ? 0x1p-5 : 0; // 5
        static constexpr real tensionDamping = 1 ? 0x1p-20 : 0; // 20

        static constexpr size_t base = Grain::base+Grain::capacity;
        static constexpr size_t capacity = 2048;
        buffer<vec3> position { capacity };
        buffer<vec3> velocity { capacity };
        buffer<array<Contact>> contacts { capacity };
        size_t count = 0;

        vec3 surfaceVelocity(size_t i, vec3) const { return velocity[i]; }
    } wire;

    // Update
    const size_t capacity = grain.capacity+wire.capacity;
    static constexpr bool implicit = true; //  (m - δt²·∂xf - δt·∂vf) δv = δt·(f + δt·∂xf·v) else m δv = δt f
    Matrix M { implicit ? capacity*3 : 0};
    buffer<vec3> F { capacity };

    static constexpr real dt = 1./256; // 256
    size_t timeStep = 0;
    bool stop = false;

    /// Evaluates contact penalty between two objects
    template<Type tA, Type tB> void contact(const tA& A, size_t a, const tB& B, size_t b) {
        pair<vec3, vec3> c = closest(A, a, B, b);
        vec3 relativePosition = c.a - c.b;
        real length = ::length(relativePosition);
        real restLength = A.thickness + B.thickness;
        if(length >= restLength) return;
        // Stiffness
        constexpr real E = 1/(1/tA::normalStiffness+1/tB::normalStiffness);
        constexpr real R = 1/(tA::curvature+tB::curvature);
        const real Ks = E*sqrt(R)/**sqrt(x0-xl)*/;
        // Damping
        constexpr real Kb = 1/(1/tA::normalDamping+1/tB::normalDamping);
        //if(!length) return; // FIXME
        assert_(length, a, c.a, c.b, b);
        vec3 normal = relativePosition/length; // A -> B
        vec3 relativeVelocity = A.surfaceVelocity(a, -normal) - B.surfaceVelocity(b, normal); // A / B
        real fN = spring(A, a, B, b, Ks, length, restLength, Kb, normal, relativeVelocity);
        vec3 midPoint = (c.a+c.b)/2.;
        {Contact& c = A.contacts[a].add(Contact{b, midPoint});
            c.lastUpdate = timeStep;
            c.normal = normal;
            c.fN = fN;
            c.relativeVelocity = relativeVelocity;
            c.currentPosition = midPoint;
        }
    }
    template<Type tA, Type tB> real spring(const tA& A unused, size_t a, const tB& B unused, size_t b,
                                           real Ks, real length, real restLength, real Kb, vec3 normal, vec3 relativeVelocity) {
        real fS = - Ks * (length - restLength);
        real fB = 0; //- Kb * dot(normal, relativeVelocity); // Damping
        vec3 f = (fS + fB) * normal;
        if(implicit) {
            mat3 o = outer(normal, normal);
            assert_(length);
            mat3 dxf = - Ks * ((1 - restLength/length)*(1 - o) + o);
            mat3 dvf = - Kb * o;
            for(int i: range(3)) {
                for(int j: range(3)) {
                    if(tA::capacity) M((tA::base+a)*3+i, (tA::base+a)*3+j) += - dt*dt*dxf(i, j) - dt*dvf(i, j);
                    if(tB::capacity) M((tB::base+b)*3+i, (tB::base+b)*3+j) -= - dt*dt*dxf(i, j) - dt*dvf(i, j);
                }
                if(0) {
                    if(tA::capacity) F[tA::base+a][i] += dt*f[i] + dt*dt*dxf(i, i) * A.velocity[a][i];
                    if(tB::capacity) F[tB::base+b][i] -= dt*f[i] + dt*dt*dxf(i, i) * B.velocity[b][i];
                } else {
                    if(tA::capacity) F[tA::base+a][i] += dt*f[i] + dt*dt*dxf(i, i) * relativeVelocity[i];
                    if(tB::capacity) F[tB::base+b][i] -= dt*f[i] + dt*dt*dxf(i, i) * relativeVelocity[i];
                }
            }
            //if((void*)&A == (void*)&grain && (void*)&B == (void*)&grain) {
                if(restLength/length > 1.01 || ::length(relativeVelocity)*dt>grain.radius/2 ||
                        ::length(F[tA::base+a]/tA::mass) > 1/.8 || ::length(F[tB::base+b]/tB::mass) > 1/.8) {
                    log(a, b, restLength/length, relativeVelocity, F[tA::base+a]/grain.mass, F[tB::base+b]/grain.mass,
                            dt*fS/grain.mass);
                    //stop = true;
                }
            //}
        } else {
            if(tA::capacity) F[tA::base+a] += dt*f;
            if(tB::capacity) F[tB::base+b] -= dt*f;
        }
        return fS + fB;
    }
};
constexpr real System::Floor::thickness;
constexpr real System::Side::thickness;
constexpr real System::Grain::radius;
constexpr real System::Grain::mass;
constexpr real System::Grain::thickness;
constexpr real System::Grain::friction;
constexpr real System::Wire::radius;
constexpr real System::Wire::mass;
constexpr real System::Wire::thickness;
constexpr real System::Wire::friction;
constexpr real System::Wire::tensionDamping;
constexpr real System::Wire::tensionStiffness;
constexpr real System::Wire::internodeLength;

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
        return int3(vec3(size)/2. * (vec3(1,1,0)+p));
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

    template<Type T> void update(const ref<vec3>& dv, Grid& grid, T& t, size_t i) {
        t.velocity[i] += dv[t.base+i];
        if(length(t.velocity[i]) > 1 || length(dv[t.base+i]) > 1) {
            log(i, t.position[i], "v", t.velocity[i], dv[t.base+i], F[t.base+i]/grain.mass,
                    length(F[t.base+i]/grain.mass));
            stop=true;
            return;
        }
        size_t oldCell = grid.index(t.position[i]);
        t.position[i] += dt*t.velocity[i];
        if(!(t.position[i].x == clamp(-1., t.position[i].x, 1.) &&
             t.position[i].y == clamp(-1., t.position[i].y, 1.) &&
             t.position[i].z == clamp(0., t.position[i].z, 2.))) {
            log(i, "p", t.position[i], t.velocity[i]);
            stop=true;
        }
        t.position[i].x = clamp(-1., t.position[i].x, 1.-0x1p-12);
        t.position[i].y = clamp(-1., t.position[i].y, 1.-0x1p-12);
        t.position[i].z = clamp(0., t.position[i].z, 2.-0x1p-12);
        size_t newCell = grid.index(t.position[i]);
        if(oldCell != newCell) {
            grid[oldCell].remove(1+i);
            grid[newCell].append(1+i);
        }
    }

    // Process
    static constexpr real winchRadius = Side::initialRadius /*- Grain::radius*/ - Wire::radius;
    static constexpr real pourRadius = Side::initialRadius - Grain::radius;
    static constexpr real winchRate = 4, winchSpeed = 1./2;
    real winchAngle = 0, pourHeight = Grain::radius, wireLength = 0, lastLength = 0;
    Random random;
    bool pour = true;

    /*// Performance
    int64 lastReport = realTime();
    Time totalTime {true}, solveTime, floorTime;
    Time grainTime, wireContactTime, wireFrictionTime, grainIntegrationTime, wireIntegrationTime;*/


    // Single implicit Euler time step
    void step() {
        log("");
        // Process
        if(pour && (pourHeight>=2 || grain.count == grain.capacity || wire.count == wire.capacity)) {
            log("Release", wire.count, grain.count);
            pour = false;
            side.currentRadius *= 2;
        }
        if(pour) {
            // Generates falling grain (pour)
            if(1) for(;;) {
                vec3 p(random()*2-1,random()*2-1, pourHeight);
                //vec3 p(0,0, grain.count ? 2-grain.radius : grain.radius);
                if(length(p.xy())>1) continue;
                vec3 newPosition (pourRadius*p.xy(), p.z); // Within cylinder
                for(vec3 p: wire.position.slice(0, wire.count))
                    if(length(p - newPosition) < grain.radius+wire.radius) goto break2_;
                for(vec3 p: grain.position.slice(0, grain.count))
                    if(length(p - newPosition) < grain.radius+grain.radius) goto break2_;
                size_t i = grain.count; grain.count++;
                grain.position[i] = newPosition;
                grain.velocity[i] = 0;
                grain.contacts.set(i);
                grainGrid[grainGrid.index(grain.position[i])].append(1+i);
                grain.rotation[i] = quat();
                grain.angularVelocity[i] = 0;
                grain.torque[i] = 0;
                break;
            }
            break2_:;
            // Generates wire (winch)
            winchAngle += winchRate * dt;
            pourHeight += winchSpeed * grain.radius * dt;
            wireLength += winchRadius * winchRate * dt;
            vec3 winchPosition (winchRadius*cos(winchAngle), winchRadius*sin(winchAngle),
                                pourHeight+Grain::radius);

            // Moves winch obstacle keeping spawn clear
            if(0) {
                size_t oldCell = grainGrid.index(grain.position[0]);
                grain.position[0] = vec3(winchPosition.xy(), winchPosition.z+grain.radius-wire.radius);
                grain.velocity[0] = 0;
                size_t newCell = grainGrid.index(grain.position[0]);
                if(oldCell != newCell) {
                    grainGrid[oldCell].remove(1+0);
                    grainGrid[newCell].append(1+0);
                }
            }
            vec3 lastPosition = wire.count ? wire.position[wire.count-1] : vec3(winchRadius, 0, pourHeight);
            vec3 r = winchPosition - lastPosition;
            real l = length(r);
            if(0 && l > wire.internodeLength*(1+1./4)) {
                lastLength = wireLength;
                size_t i = wire.count; wire.count++;
                vec3 pos = lastPosition + wire.internodeLength/l*r;
                wire.position[i] = pos;
                wire.velocity[i] = 0;
                wire.contacts.set(i);
                wireGrid[wireGrid.index(wire.position[i])].append(1+i);
            }
        }

        // Initialization: gravity, bound
        M.clear();
        constexpr vec3 g {0, 0, -1};
        for(size_t i: range(grain.count)) {
            grain.torque[i] = 0;
            if(implicit) for(int c: range(3)) M((grain.base+i)*3+c, (grain.base+i)*3+c) = grain.mass;
            F[grain.base+i] = dt * grain.mass * g;
            contact(grain, i, floor, 0);
            contact(grain, i, side, 0);
        }
        for(size_t i: range(wire.count)) {
            if(implicit) for(int c: range(3)) M((wire.base+i)*3+c, (wire.base+i)*3+c) = wire.mass;
            F[wire.base+i] = dt * wire.mass * g;
            contact(wire, i, floor, 0);
            //contact(wire, i, side, 0);
        }

        // Wire tension
        /*for(size_t i: range(1, wire.count)) {
            size_t a = i-1, b = i;
            vec3 relativePosition = wire.position[b] - wire.position[a];
            real length = ::length(relativePosition);
            //if(!length) continue; // FIXME
            assert_(length, "tension", i, wire.position[a], wire.position[b]);
            real restLength = wire.internodeLength;
            vec3 normal = relativePosition/length;
            vec3 relativeVelocity = wire.velocity[a] - wire.velocity[b];
            spring<Wire, Wire>(a, b, wire.tensionStiffness, length, restLength, wire.tensionDamping, normal, relativeVelocity);
        }*/

        // Bending resistance
        /*for(size_t i: range(1, wire.count-1)) {
            vec3 A = position[wire+i-1], B = position[wire+i], C = position[wire+i+1];
            vec3 B0 = (A+C)/2.f;
            vec3 x = B0 - B; // Outward from A to B
            real xl = length(x);
            real x0 = 0;
            vec3 nx = x/xl;
            vec3 v = velocity[wire+i]  - (velocity[wire+i-1]+velocity[wire+i+1])/2.f; // Velocity of B relative to A+C
            spring(wireBendStiffness, wireBendDamping, x0, xl, nx, wire+i, v);
        }*/

        // Grain - Grain contacts
        //grainTime.start();
        parallel_for(grain.count, [this](uint, int a) {
            int3 index = grainGrid.index3(grain.position[a]);
            for(int z: range(max(0, index.z-1), min(index.z+2, grainGrid.size.z))) {
                for(int y: range(max(0, index.y-1), min(index.y+2, grainGrid.size.y))) {
                    for(int x: range(max(0, index.x-1), min(index.x+2, grainGrid.size.x))) {
                        Grid::List list = grainGrid[grainGrid.index(x, y, z)];
                        for(size_t i=0; i<grainGrid.cellCapacity; i++) {
                            int b = list[i]-1;
                            if(!(a < b)) break; // Single contact per pair, until 0
                            contact(grain, a, grain, b);
                        }
                    }
                }
            }
        });
        //grainTime.stop();

        /*// Grain - Grain friction
        parallel_for(grain.count, [this](uint, size_t a) {
            for(size_t i=0; i<grain.contacts[a].size;) {
                Contact& contact = grain.contacts[a][i];
                if(contact.lastUpdate != timeStep) { grain.contacts[a].removeAt(i); continue; } else i++;
                size_t b = contact.index;
                vec3 fT = contact.friction(grain.friction);
                 F[grain.base+a] += dt*fT;
                 grain.torque[a] += cross(grain.radius*contact.normal, fT);
                 F[grain.base+b] -= dt*fT;
                 grain.torque[b] -= cross(grain.radius*contact.normal, fT);
                }
            }
        });*/

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
                                contact(wire, a, wire, b);
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
                                if(b<0) break; // until 0
                                //contact(wire, a, grain, b);
                            }
                        }
                    }
                }
            }
        });

        /*// Wire - Grain friction
        parallel_for(wire.count, [this](uint, size_t a) {
            for(size_t i=0; i<wire.contacts[a].size;) {
                Contact& c = wire.contacts[a][i];
                if(c.lastUpdate != timeStep) { wire.contacts[a].removeAt(i); continue; } else i++;
                size_t b = c.index;
                //const real bFriction = b >= grain ? grainFriction : wireFriction; vec3 fT = c.friction((wireFriction+bFriction)/2);
                vec3 fT = c.friction((wire.friction+grain.friction)/2);
                F[(wire.base+a] += dt*fT;
                F[(grain.base+b] -= dt*fT;
                grain.torque[b] -= cross(grain.radius*c.normal, fT);
            }
        });*/

        //solveTime.start();
        buffer<vec3> dv;
        if(implicit) dv = cast<vec3>(UMFPACK(M).solve(cast<real>(F)));
        else {
            dv = buffer<vec3>(F.capacity);
            for(size_t i: range(grain.count)) dv[grain.base+i] = F[grain.base+i] / grain.mass;
            for(size_t i: range(wire.count)) dv[wire.base+i] = F[wire.base+i] / wire.mass;
        }
        //solveTime.stop();

        for(size_t i: range(grain.count)) update(dv, grainGrid, grain, i);
        for(size_t i: range(wire.count)) update(dv, wireGrid, wire, i);

        // PCDM rotation integration
        //grainIntegrationTime.start();
        parallel_for(grain.count, [this](uint, size_t i) {
            static constexpr real I (2./3*Grain::mass*sq(Grain::radius)); // mat3
            vec3 w = (grain.rotation[i].conjugate() * quat{0, grain.angularVelocity[i]} * grain.rotation[i]).v; // Local
            vec3 t = (grain.rotation[i].conjugate() * quat{0, grain.torque[i]} * grain.rotation[i]).v; // Local
            vec3 dw = 1/I * (t - cross(w, I*w));
            vec3 w4 = w + dt/4*dw;
            real w4l = length(w4);
            // Prediction (multiplicative update)
            quat qp = w4l ? quat{cos(dt/4*w4l), sin(dt/4*w4l)*w4/w4l} * grain.rotation[i] : grain.rotation[i];
            vec3 w2 = w + dt/2*dw;
            vec3 w2w = (qp * quat{0, w2} * qp.conjugate()).v; // Global
            real w2wl = length(w2w);
            // Correction (multiplicative update)
            grain.rotation[i] = w2wl ? quat{cos(dt/2*w2wl), sin(dt/2*w2wl)*w2w/length(w2w)} * grain.rotation[i] : grain.rotation[i];
            grain.angularVelocity[i] = (grain.rotation[i] * quat{0, w + dt*dw} * grain.rotation[i].conjugate()).v; // Global
        });
        //grainIntegrationTime.stop();

        timeStep++;
    }
};

struct SimulationView : Simulation, Widget, Poll {
    void step() {
        Simulation::step();
        window->render();
#if 0
        if(realTime() > lastReport+2e9) {
            log("solve",str(solveTime, totalTime)/*, "grain",str(grainTime, solveTime), "wire",str(wireContactTime, solveTime),
                 "wire",str(wireFrictionTime, solveTime), "floor",str(floorTime, solveTime),
                "grain integration", str(grainIntegrationTime, solveTime), "wire integration", str(wireIntegrationTime, solveTime)*/);
            lastReport = realTime();
        }
#endif
        if(!Simulation::stop) queue();
    }
    void event() { step(); }

    unique<Window> window = ::window(this, 1050);
    Thread simulationThread;
    SimulationView() : Poll(0, POLLIN, simulationThread) {
        step();
        simulationThread.spawn();

        //window->actions[Space] = [this]{ writeFile(str(time), encodePNG(render(1050, graphics(1050))), home()); };
        window->actions[Return] = [this]{
            if(pour) { pour = false; side.currentRadius *= 2; window->setTitle("2"); log("Release", wire.count, grain.count); }
        };
        window->actions[Escape] = [this]{ log(wire.count, grain.count); requestTermination(); };

        glDepthTest(true);
    }
    vec2 sizeHint(vec2) { return 1050; }
    shared<Graphics> graphics(vec2) {
        mat4 viewProjection = mat4() .scale(vec3(2, 2, -1)) .rotateX(viewRotation.y) .rotateZ(viewRotation.x)
                .translate(vec3(0,0, -1./4));

        if(grain.count) {
            const size_t grainCount = grain.count;
            buffer<float3> positions {grainCount*6};
            for(size_t i: range(grainCount)) {
                // FIXME: GPU quad projection
                float3 O (viewProjection*grain.position[i]);
                float3 min = O - float3(float2(2*grain.radius), 0); // Isometric
                float3 max = O + float3(float2(2*grain.radius), 0); // Isometric
                positions[i*6+0] = min;
                positions[i*6+1] = float3(max.x, min.y, O.z);
                positions[i*6+2] = float3(min.x, max.y, O.z);
                positions[i*6+3] = float3(min.x, max.y, O.z);
                positions[i*6+4] = float3(max.x, min.y, O.z);
                positions[i*6+5] = max;
            }

            static GLShader shader {::shader(), {"sphere"}};
            shader.bind();
            shader.bindFragments({"color"});
            static GLVertexArray vertexArray;
            GLBuffer positionBuffer (positions);
            vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
            vertexArray.draw(Triangles, positions.size);
        }

        if(wire.count>1) {
            const size_t wireCount = this->wire.count;
            buffer<float3> positions {(wireCount-1)*6};
            for(size_t i: range(wireCount-1)) {
                vec3 a = wire.position[i], b = wire.position[i+1];
                // FIXME: GPU quad projection
                float3 A (viewProjection*a), B (viewProjection*b);
                float2 r = B.xy()-A.xy();
                float l = length(r);
                float2 t = r/l;
                float3 n (t.y, -t.x, 0);
                float width = 2*wire.radius;
                float3 P[4] {A-width*n, A+width*n, B-width*n, B+width*n};
                positions[i*6+0] = P[0];
                positions[i*6+1] = P[1];
                positions[i*6+2] = P[2];
                positions[i*6+3] = P[2];
                positions[i*6+4] = P[1];
                positions[i*6+5] = P[3];
            }
            static GLShader shader {::shader(), {"cylinder"}};
            shader.bind();
            shader.bindFragments({"color"});
            static GLVertexArray vertexArray;
            GLBuffer positionBuffer (positions);
            vertexArray.bindAttribute(shader.attribLocation("position"_), 3, Float, positionBuffer);
            vertexArray.draw(Triangles, positions.size);
        }
        return shared<Graphics>();
    }

    // View
    vec2 lastPos; // Last cursor position to compute relative mouse movements
    vec2 viewRotation = vec2(0, -PI/2); // Current view angles (yaw,pitch)
    // Orbital ("turntable") view control
    bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        vec2 delta = cursor-lastPos; lastPos=cursor;
        if(event==Motion && button==LeftButton) {
            viewRotation += real(2.f*PI) * delta / size; //TODO: warp
            viewRotation.y= clamp(real(-PI/*2*/), viewRotation.y, 0.); // Keep pitch between [-PI, 0]
        }
        else return false;
        return true;
    }
} view;
