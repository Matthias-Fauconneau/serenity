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

// Contact

struct Contact { // 16 w
    size_t index; // Identifies contact to avoid duplicates and evaluate friction
    vec3 initialPosition; // Initial contact position
    int lastUpdate; // Time step of last update (to remove stale contacts)
    // Evaluate once to use both for normal and friction
    vec3 n; // Normal
    real fN; // Force
    vec3 v; // Relative velocity
    vec3 currentPosition; // Current contact position
    vec3 friction(const real friction) {
        vec3 fT = 0;
        {const real dynamicFriction = friction;
            vec3 t = v-dot(n, v)*n;
            real vl = length(t); // vt
            if(vl) fT -= dynamicFriction * fN * t / vl;
            assert_(isNumber(fT), "d", v, t, fN);
        }
        if(0 || length(v) < 1) { // Static friction
            vec3 x = currentPosition - initialPosition;
            vec3 t = x-dot(n, x)*n;
            real l = length(t);
            if(0 || l < 1./256) { // FIXME
                const real staticFriction = friction;
                fT -= staticFriction * abs(fN) * t;
                assert_(isNumber(fT), fN, t);
                if(l) {
                    vec3 u = t/l;
                    constexpr real tangentialDamping = 0;//0x1p-20; //FIXME
                    fT -= tangentialDamping * dot(u, v) * u;
                    assert_(isNumber(fT), "t", v, u);
                } // else static equilibrium
            }
        } else initialPosition = currentPosition; // Dynamic friction
        assert_(isNumber(fT), "f", v);
        return fT;
    }
};
bool operator==(const Contact& a, const Contact& b) { return a.index == b.index; }
String str(const Contact& c) { return str(c.index); }

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
        assert_(x == clamp(0, x, size.x-1));
        assert_(y == clamp(0, y, size.y-1));
        assert_(z == clamp(0, z, size.z-1));
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
String str(const Grid::List& o) { return str((mref<uint16>)o); }

struct DEM : Widget, Poll {
    static constexpr size_t wire = 0;
    static constexpr size_t wireCapacity = 2048;
    size_t wireCount = 0;
    static constexpr size_t grain = wire+wireCapacity;
    static constexpr size_t grainCapacity = 1024;
    size_t grainCount = 0;
    static constexpr size_t elementCapacity = grain+grainCapacity;

    Matrix M { elementCapacity*3 };
    Vector b { elementCapacity*3 };

    // Grains + Wire
    buffer<vec3> position { elementCapacity };
    buffer<vec3> velocity { elementCapacity };
    buffer<array<Contact>> contacts { elementCapacity };
    // Grains only
    buffer<quat> rotation { grainCapacity };
    buffer<vec3> angularVelocity { grainCapacity };
    buffer<vec3> torque { grainCapacity };

    // Space partition
    Grid grainGrid {32}, wireGrid {256};

    // Parameters
    static constexpr real dt = 1./256; // 256

    static constexpr real grainRadius = 1./32; // 62 mm dia
    static constexpr real grainMass = 4./3*PI*cb(grainRadius);
    static constexpr real grainYoung = 32; // 64
    static constexpr real grainNormalDamping = 1./128; // 64
    static constexpr real grainFriction = 1;

    static constexpr real wireRadius = 1./256; // 1/256 = 8 mm dia
    static constexpr real internodeLength = 4*wireRadius; // 4
    static constexpr real wireMass = internodeLength * PI * wireRadius * wireRadius;
    static constexpr real wireYoung = 1;
    static constexpr real wireNormalDamping = 0x1p-16; // 16
    static constexpr real wireFriction = 1;

    static constexpr real wireTensionStiffness = 1 ? 0x1p-5 : 0; // 6
    static constexpr real wireTensionDamping = 1 ? 0x1p-20 : 0; // 20

    static constexpr real boundYoung = 1;

    // Time
    int timeStep = 0;
    real time = 0;

    // Process
    static constexpr real moldRadius = 1./2;
    static constexpr real winchRadius = moldRadius - grainRadius - wireRadius;
    static constexpr real pourRadius = moldRadius - grainRadius;
    static constexpr real winchRate = 4, winchSpeed = 1./2;
    real boundRadius = moldRadius;
    real winchAngle = 0, pourHeight = wireRadius, wireLength = 0, lastLength = 0;
    bool pour = true;
    Random random;

    // Performance assert_(xl);
    int64 lastReport = realTime();
    Time totalTime {true}, solveTime, floorTime;
    Time grainTime, wireContactTime, wireFrictionTime, grainIntegrationTime, wireIntegrationTime;

    // Linear(FIXME) spring between two elements
    static constexpr bool implicit = false;
    real spring(real ks, real kb, real x0, real xl, vec3 xn, int i, int j, vec3 v) {
        assert_(i>=0 && j>=0);
        real fN = - ks * (xl - x0);
        vec3 f = (fN - kb * dot(xn, v)) * xn;
        if(implicit) {
            mat3 o = outer(xn, xn);
            assert_(xl, "spring", ks, kb, x0, xl, xn, i, j, v);
            if(!xl) return 0; //FIXME
            mat3 dxf = - ks * ((1 - x0/xl)*(1 - o) + o);
            mat3 dvf = - kb * o;
            for(int c0: range(3)) {
                if(implicit) for(int c1: range(3)) {
                    const real a = dt*dvf(c0, c1) + dt*dt*dxf(c0, c1);
                    M(i*3+c0, i*3+c1) -= a;
                    M(j*3+c0, j*3+c1) += a;
                }
                const real a = dt*dxf(c0, c0);
                b[i*3+c0] += dt*(  f[c0] + a*velocity[i][c0] );
                b[j*3+c0] -= dt*( f[c0] + a*velocity[j][c0] );
            }
        } else {
            for(int c0: range(3)) {
                b[i*3+c0] += dt*f[c0];
                b[j*3+c0] -= dt*f[c0];
            }
        }
        return fN;
    }
    // Linear(FIXME) spring to a fixed point
    real spring(real ks, real kb, real x0, real xl, vec3 xn, size_t i, vec3 v) {
        real fN = - ks * (xl - x0);
        vec3 f = (fN - kb * dot(xn, v)) * xn;
        assert_(isNumber(f), "f", f, ks, kb, x0, xl, xn, i, v);
        if(implicit) {
            mat3 o = outer(xn, xn);
            assert_(xl, ks, kb, x0, xl , xn, i , v);
            mat3 dxf = - ks * ((1 - x0/xl)*(1 - o) + o);
            mat3 dvf = - kb * o;
            for(int c0: range(3)) {
                for(int c1: range(3)) {
                    real a = dt*dvf(c0, c1) + dt*dt*dxf(c0, c1);
                    M(i*3+c0, i*3+c1) -= a;
                }
                const real a = dt*dxf(c0, c0);
                b[i*3+c0] += dt*( f[c0] + a*velocity[i][c0] );
            }
        } else {
            for(int c0: range(3)) b[i*3+c0] += dt*f[c0];
        }
        return fN;
    }

    void contact(size_t a, size_t b, vec3 n, vec3 v, real fN, vec3 p) {
        auto& c = contacts[a].add(Contact{b,p,0,0,0,0,0});
        c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition = p;
    }

    // FIXME: factorize contacts

    // Grain - Bound contact
    void contactGrainBound(size_t a) {
        {// Floor
            real xl = grainRadius + position[a].z;
            assert_(xl);
            real x0 = 2 * grainRadius;
            if(xl < x0) {
                vec3 xn (0,0,1);
                vec3 v = velocity[a] + cross(angularVelocity[a-grain], grainRadius*(+xn));
                constexpr real E = boundYoung/2, R = grainRadius;
                spring(4/3*E*sqrt(R)*sqrt(x0-xl), grainNormalDamping, x0, xl, xn, a, v);
            }
        }
        {// Side
            vec3 x = vec3(position[a].xy(), 0);
            real xl = length(x);
            assert_(xl);
            real x0 = boundRadius - grainRadius;
            if(x0 < xl) {
                vec3 xn = x/xl;
                vec3 v = velocity[a] + cross(angularVelocity[a-grain], grainRadius*(+xn));
                constexpr real E = boundYoung/2, R = grainRadius;
                spring(4/3*E*sqrt(R)*sqrt(xl-x0), grainNormalDamping, x0, xl, xn, a, v);
            }
        }
    }
    // Grain - Grain contact
    void contactGrainGrain(size_t a, size_t b) {
        vec3 x = position[a] - position[b];
        real xl = length(x);
        assert_(xl);
        real x0 = grainRadius + grainRadius;
        if(xl < x0) {
            vec3 n = x/xl;
            vec3 v = (velocity[a] + cross(angularVelocity[a-grain], grainRadius*(+n)))
                          - (velocity[b] + cross(angularVelocity[b-grain], grainRadius*(-n)));
            constexpr real E = grainYoung/2, R = grainRadius/2;
            real fN = spring(4/3*E*sqrt(R)*sqrt(x0-xl), grainNormalDamping, x0, xl, n, a, b, v); //FIXME
            vec3 p = (position[a]+grainRadius*n + position[b]+grainRadius*(-n))/2.;
            contact(a, b, n, v, fN, p);
        }
    }

    // Wire - Bound contact
    void contactWireBound(size_t a) {
        {// Floor
            real xl = wireRadius + position[a].z;
            assert_(xl);
            real x0 = 2 * wireRadius;
            if(xl < x0) {
                vec3 n (0,0,1);
                vec3 v = velocity[a];
                assert_(isNumber(v), a);
                constexpr real E = boundYoung/2, R = wireRadius;
                real fN = spring(4/3*E*sqrt(R)*sqrt(x0-xl), wireNormalDamping, x0, xl, n, a, v);
                vec3 p (position[a].xy(), (position[a].z-wireRadius)/2);
                contact(a, invalid, n, v, fN, p);
            }
        }
        {//Side
            vec3 x = vec3(position[a].xy(), 0);
            real xl = length(x);
            assert_(xl);
            real x0 = boundRadius + wireRadius;
            if(x0 < xl) {
                vec3 xn = x/xl;
                vec3 v = velocity[a];
                constexpr real E = boundYoung/2, R = wireRadius;
                spring(4/3*E*sqrt(R)*sqrt(xl-x0), wireNormalDamping, x0, xl, xn, a, v);
            }
        }
    }
    // Wire - Wire vertices contact
    void contactWireWire(size_t a, size_t b) {
        vec3 x = position[a] - position[b];
        real xl = length(x);
        //assert_(xl, "ww", a, position[a], b, position[b]);
        if(!xl) return; // FIXME
        real x0 = wireRadius + wireRadius;
        if(xl < x0) {
            vec3 xn = x/xl;
            vec3 v = velocity[a] - velocity[b];
            constexpr real E = wireYoung/2, R = wireRadius/2;
            spring(4/3*E*sqrt(R)*sqrt(x0-xl), wireNormalDamping, x0, xl, xn, a, b, v);
            // FIXME: Contact
        }
    }
    // Wire vertex - Grain contact
    void contactWireGrain(size_t a, size_t b) {
        vec3 x = position[a] - position[b];
        real l = length(x);
        assert_(l);
        real x0 = wireRadius + grainRadius;
        if(l < x0) {
            vec3 n = x/l;
            vec3 v = velocity[a] - (velocity[b] /*+ cross(angularVelocity[b-grain], grainRadius*(-n))*/);
            constexpr real E = wireYoung, R = wireRadius;
            real fN = spring(4/3*E*sqrt(R)*(x0-l), wireNormalDamping, x0, l, n, a, b, v);
            vec3 p = (position[a]+wireRadius*n + position[b]+grainRadius*(-n))/2.;
            contact(a, b, n, v, fN, p);
        }
    }

    // Single implicit Euler time step
    void step() {
        // Process
        if(pour && (pourHeight>=2 || grainCount == grainCapacity || wireCount == wireCapacity)) {
            log("Release", wireCount, grainCount);
            pour = false;
            boundRadius *= 2;
        }
        if(pour) {
            // Generates falling grain (pour)
            if(1) for(;;) {
                //for(vec3 p: position.slice(grain, grainCount)) pourHeight = max(pourHeight, p.z);
                vec3 p(random()*2-1,random()*2-1, pourHeight);
                if(length(p.xy())>1) continue;
                vec3 newPosition (pourRadius*p.xy(), p.z);
                for(vec3 p: position.slice(wire, wireCount)) if(length(p - newPosition) < grainRadius+wireRadius) goto break2_;
                for(vec3 p: position.slice(grain, grainCount)) if(length(p - newPosition) < grainRadius+grainRadius) goto break2_;
                size_t i = grainCount; grainCount++;
                position[grain+i] = newPosition;
                velocity[grain+i] = 0;
                contacts.set(grain+i);
                grainGrid[grainGrid.index(position[grain+i])].append(1+grain+i);
                rotation[i] = quat();
                angularVelocity[i] = 0;
                torque[i] = 0;
                break;
            }
            break2_:;
            // Generates wire (winch)
            winchAngle += winchRate * dt;
            pourHeight += winchSpeed * grainRadius * dt;
            wireLength += winchRadius * winchRate * dt;
            vec3 winchPosition (winchRadius*cos(winchAngle), winchRadius*sin(winchAngle), pourHeight);
            {size_t oldCell = grainGrid.index(position[grain+0]);
                position[grain+0] = vec3(winchPosition.xy(), winchPosition.z+grainRadius-wireRadius); // Keeps spawn clear
                velocity[grain+0] = 0;
                size_t newCell = grainGrid.index(position[grain+0]);
                if(oldCell != newCell) {
                    grainGrid[oldCell].remove(1+grain+0);
                    grainGrid[newCell].append(1+grain+0);
                }
            }
            /*for(vec3 p: position.slice(grain, grainCount)) if(length(winchPosition - p) < grainRadius + wireRadius) {
                winchPosition.z = p.z+sqrt(sq(grainRadius+wireRadius)-sq((winchPosition - p).xy()));
            }*/
            vec3 lastPosition = wireCount ? position[wire+wireCount-1] : vec3(winchRadius, 0, pourHeight);
            vec3 r = winchPosition - lastPosition;
            real l = length(r);
            if(l > internodeLength*(1+1./4)) {
                lastLength = wireLength;
                size_t i = wireCount; wireCount++;
                vec3 pos = lastPosition + internodeLength/l*r;
                /*for(vec3 p: position.slice(grain, grainCount)) if(length(pos - p) < grainRadius + wireRadius) {
                    pos.z = p.z+sqrt(sq(grainRadius+wireRadius)-sq((pos - p).xy()));
                }*/
                position[wire+i] = pos;

            /*if(wireLength > lastLength+internodeLength) {
                lastLength = wireLength;
                size_t i = wireCount; wireCount++;
                position[wire+i] = winchPosition;*/
                velocity[wire+i] = 0;
                contacts.set(wire+i);
                wireGrid[wireGrid.index(position[wire+i])].append(1+wire+i);
            }
        }

        // Initialization: gravity, bound
        M.clear(); //b.clear();
        constexpr vec3 g {0, 0, -1};
        for(size_t i: range(grainCount)) {
            torque[i] = 0;
            vec3 force = grainMass * g;
            for(int c: range(3)) {
                M((grain+i)*3+c, (grain+i)*3+c) = grainMass;
                b[(grain+i)*3+c] = dt * force[c];
            }
            contactGrainBound(grain+i);
        }
        for(size_t i: range(wireCount)) {
            vec3 force = wireMass * g;// / 16.f; // FIXME
            for(int c: range(3)) {
                M((wire+i)*3+c, (wire+i)*3+c) = wireMass;
                b[(wire+i)*3+c] = dt * force[c];
            }
            contactWireBound(wire+i);
        }

        // Wire tension
        if(1) for(size_t i: range(1, wireCount)) {
            vec3 x = position[wire+i] - position[wire+i-1]; // Outward from A to B
            real xl = length(x);
            if(!xl) continue; // FIXME
            assert_(xl, "tension", i, position[wire+i], position[wire+i-1]);
            real x0 = internodeLength;
            vec3 nx = x/xl;
            vec3 v = velocity[wire+i] - velocity[wire+i-1]; // Velocity of A relative to B
            spring(wireTensionStiffness/**sq(x0-xl)*/, wireTensionDamping, x0, xl, nx, wire+i, wire+i-1, v); // FIXME
        }

        // Bending resistance
        /*for(size_t i: range(1, wireCount-1)) {
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
        grainTime.start();
        parallel_for(grain, grain+grainCount, [this](uint, int a) {
            if(1) {
                int3 index = grainGrid.index3(position[a]);
                for(int z: range(max(0, index.z-1), min(index.z+2, grainGrid.size.z))) {
                    for(int y: range(max(0, index.y-1), min(index.y+2, grainGrid.size.y))) {
                        for(int x: range(max(0, index.x-1), min(index.x+2, grainGrid.size.x))) {
                            Grid::List list = grainGrid[grainGrid.index(x, y, z)];
                            for(size_t i=0; i<grainGrid.cellCapacity; i++) {
                                int b = list[i]-1;
                                if(!(a < b)) break; // Single contact per pair, until 0
                                contactGrainGrain(a, b);
                            }
                        }
                    }
                }
            } else {
                for(int b: range(grain, grain+grainCount)) if(a<b) contactGrainGrain(a, b);
            }
        });
        grainTime.stop();

        // Contact frictions
        parallel_for(grain, grainCount, [this](uint, size_t a) {
            for(size_t i=0; i<contacts[a].size;) {
                Contact& c = contacts[a][i];
                if(c.lastUpdate != timeStep) { contacts[a].removeAt(i); continue; } else i++;
                size_t b = c.index;
                vec3 fT = c.friction(grainFriction);
                for(int c0: range(3)) { // FIXME: implicit
                    this->b[a*3+c0] += dt*fT[c0];
                    torque[a-grain] += cross(grainRadius*c.n, fT);
                    this->b[b*3+c0] -= dt*fT[c0];
                    torque[b-grain] -= cross(grainRadius*c.n, fT);
                }
            }
        });

        // Wire - Wire contacts
        wireContactTime.start();
        parallel_for(wire, wire+wireCount, [this](uint, int a) {
            {
                int3 index = wireGrid.index3(position[a]);
                for(int z: range(max(0, index.z-1), min(index.z+2, wireGrid.size.z))) {
                    for(int y: range(max(0, index.y-1), min(index.y+2, wireGrid.size.y))) {
                        for(int x: range(max(0, index.x-1), min(index.x+2, wireGrid.size.x))) {
                            Grid::List list = wireGrid[wireGrid.index(x, y, z)];
                            for(size_t i=0; i<wireGrid.cellCapacity; i++) {
                                int b = list[i]-1;
                                if(!(a+1 < b)) break; // No adjacent, single contact per pair, until 0
                                //contactWireWire(a, b);
                            }
                        }
                    }
                }
            }
            if(1) {
                int3 index = grainGrid.index3(position[a]);
                for(int z: range(max(0, index.z-1), min(index.z+2, grainGrid.size.z))) {
                    for(int y: range(max(0, index.y-1), min(index.y+2, grainGrid.size.y))) {
                        for(int x: range(max(0, index.x-1), min(index.x+2, grainGrid.size.x))) {
                            Grid::List list = grainGrid[grainGrid.index(x, y, z)];
                            for(size_t i=0; i<grainGrid.cellCapacity; i++) {
                                int b = list[i]-1;
                                if(b<0) break; // until 0
                                contactWireGrain(a, b);
                            }
                        }
                    }
                }
            } else {
                for(int b: range(grain, grain+grainCount)) contactWireGrain(a, b);
            }
        });
        wireContactTime.stop();

        // Contact frictions
        wireFrictionTime.start();
        parallel_for(wire+1, wire+wireCount, [this](uint, size_t a) {
            for(size_t i=0; i<contacts[a].size;) {
                Contact& c = contacts[a][i];
                if(c.lastUpdate != timeStep) { contacts[a].removeAt(i); continue; } else i++;
                size_t b = c.index;
                //const real bFriction = b >= grain ? grainFriction : wireFriction;
                vec3 fT = c.friction(1/*(1/wireFriction+1/bFriction)*/);
                for(int c0: range(3)) { // FIXME: implicit
                    this->b[a*3+c0] += dt*fT[c0];
                    if(b != invalid) {
                        this->b[b*3+c0] -= dt*fT[c0];
                        if(b >= grain) torque[b-grain] -= cross(grainRadius*c.n, fT);
                    }
                }
            }
        });
        wireFrictionTime.stop();

        // Implicit Euler
        solveTime.start();
        Vector dv = UMFPACK(M).solve(b);
        solveTime.stop();

        // Anchors both wire ends
        for(int c: range(3)) dv[(wire+0                  )*3+c] = 0;
        //for(int c: range(3)) dv[(wire+wireCount-1)*3+c] = 0;

        auto update = [this, &dv](Grid& grid, size_t i) {
            for(int c: range(3)) {
                assert_(isNumber(dv[i*3+c]));
                velocity[i][c] += dv[i*3+c];
                //velocity[i][c] *= (1-dt); // Viscosity
                assert_(isNumber(velocity[i][c]), dv[i*3+c]);
            }
            size_t oldCell = grid.index(position[i]);
            for(int c: range(3)) position[i][c] += dt*velocity[i][c];
            position[i].x = clamp(-1., position[i].x, 1.-0x1p-12);
            position[i].y = clamp(-1., position[i].y, 1.-0x1p-12);
            position[i].z = clamp(0., position[i].z, 2.-0x1p-12);
            size_t newCell = grid.index(position[i]);
            if(oldCell != newCell) {
                grid[oldCell].remove(1+i);
                grid[newCell].append(1+i);
            }
        };
        for(size_t i: range(grain, grain+grainCount)) update(grainGrid, i);
        for(size_t i: range(wire,wire+wireCount)) update(wireGrid, i);

        // PCDM rotation integration
        //grainIntegrationTime.start();
        parallel_for(0, grainCount, [this](uint, size_t i) {
            static constexpr real I (2./3*grainMass*sq(grainRadius)); // mat3
            vec3 w = (rotation[i].conjugate() * quat{0, angularVelocity[i]} * rotation[i]).v; // Local
            vec3 t = (rotation[i].conjugate() * quat{0, torque[i]} * rotation[i]).v; // Local
            vec3 dw = 1/I * (t - cross(w, I*w));
            vec3 w4 = w + dt/4*dw;
            real w4l = length(w4);
            // Prediction (multiplicative update)
            quat qp = w4l ? quat{cos(dt/4*w4l), sin(dt/4*w4l)*w4/w4l} * rotation[i] : rotation[i];
            vec3 w2 = w + dt/2*dw;
            vec3 w2w = (qp * quat{0, w2} * qp.conjugate()).v; // Global
            real w2wl = length(w2w);
            // Correction (multiplicative update)
            rotation[i] = w2wl ? quat{cos(dt/2*w2wl), sin(dt/2*w2wl)*w2w/length(w2w)} * rotation[i] : rotation[i];
            angularVelocity[i] = (rotation[i] * quat{0, w + dt*dw} * rotation[i].conjugate()).v; // Global
        });
        //grainIntegrationTime.stop();

        time += dt;
        timeStep++;
        window->render();
        if(realTime() > lastReport+2e9) {
            log("solve",str(solveTime, totalTime)/*, "grain",str(grainTime, solveTime), "wire",str(wireContactTime, solveTime),
                 "wire",str(wireFrictionTime, solveTime), "floor",str(floorTime, solveTime),
                "grain integration", str(grainIntegrationTime, solveTime), "wire integration", str(wireIntegrationTime, solveTime)*/);
            lastReport = realTime();
        }
        queue();
    }
    void event() { step(); }

    unique<Window> window = ::window(this, 1050);
    Thread solverThread;
    DEM() : Poll(0, POLLIN, solverThread) {
        b.clear();
        step();
        solverThread.spawn();

        glDepthTest(true);

        window->actions[Space] = [this]{ writeFile(str(time), encodePNG(render(1050, graphics(1050))), home()); };
        window->actions[Return] = [this]{
            if(pour) { pour = false; boundRadius *= 2; window->setTitle("2"); log("Release", wireCount, grainCount); }
        };
        window->actions[Escape] = [this]{ log(wireCount, grainCount); requestTermination(); };
    }
    vec2 sizeHint(vec2) { return 1050; }
    shared<Graphics> graphics(vec2) {
        mat4 viewProjection = mat4() .scale(vec3(2, 2, -1)) .rotateX(viewRotation.y) .rotateZ(viewRotation.x)
                .translate(vec3(0,0, -1./4));

        const size_t grainCount = this->grainCount;
        if(grainCount) {
            buffer<float3> positions {grainCount*6};
            for(size_t i: range(grainCount)) {
                // FIXME: GPU quad projection
                float3 O (viewProjection*position[grain+i]);
                float3 min = O - float3(float2(2*grainRadius), 0); // Isometric
                float3 max = O + float3(float2(2*grainRadius), 0); // Isometric
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

        const size_t wireCount = this->wireCount;
        if(wireCount>1) {
            buffer<float3> positions {(wireCount-1)*6};
            for(size_t i: range(wireCount-1)) {
                vec3 a = position[wire+i], b = position[wire+i+1];
                // FIXME: GPU quad projection
                float3 A (viewProjection*a), B (viewProjection*b);
                float2 r = B.xy()-A.xy();
                float l = length(r);
                float2 t = r/l;
                float3 n (t.y, -t.x, 0);
                float width = 2*wireRadius;
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
