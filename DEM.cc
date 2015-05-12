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

// -> vector.h
template<template<Type> /*Type*/class V, Type T, uint N> inline /*constexpr*/ float length(const vec<V,T,N>& a) { return sqrt(dot(a,a)); }

struct quat {
    float s; vec3 v;
    quat conjugate() const { return {s, -v}; }
};
quat operator*(quat p, quat q) { return {p.s*q.s - dot(p.v, q.v), p.s*q.v + q.s*p.v + cross(p.v, q.v)}; }
String str(quat q) { return "["+str(q.s, q.v)+"]"; }

// -> distance.h
void closest(vec3 a1, vec3 a2, vec3 b1, vec3 b2, vec3& A, vec3& B) {
    vec3 u = a2 - a1;
    vec3 v = b2 - b1;
    vec3 w = a1 - b1;
    float  a = dot(u,u);        // always >= 0
    float  b = dot(u,v);
    float  c = dot(v,v);        // always >= 0
    float  d = dot(u,w);
    float  e = dot(v,w);
    float  D = a*c - b*b;       // always >= 0
    float  sD = D;      // sc = sN / sD, default sD = D >= 0
    float  tD = D;      // tc = tN / tD, default tD = D >= 0

    // Compute the line parameters of the two closest points
    float sN, tN;
    if (D < __FLT_EPSILON__) { // the lines are almost parallel
        sN = 0;        // force using point P0 on segment S1
        sD = 1;        // to prevent possible division by 0.0 later
        tN = e;
        tD = c;
    }
    else {                // get the closest points on the infinite lines
        sN = (b*e - c*d);
        tN = (a*e - b*d);
        if (sN < 0) {       // sc < 0 => the s=0 edge is visible
            sN = 0;
            tN = e;
            tD = c;
        }
        else if (sN > sD) {  // sc > 1 => the s=1 edge is visible
            sN = sD;
            tN = e + b;
            tD = c;
        }
    }

    if (tN < 0) {           // tc < 0 => the t=0 edge is visible
        tN = 0;
        // recompute sc for this edge
        if (-d < 0) sN = 0;
        else if (-d > a) sN = sD;
        else { sN = -d; sD = a; }
    }
    else if (tN > tD) {      // tc > 1 => the t=1 edge is visible
        tN = tD;
        // recompute sc for this edge
        if ((-d + b) < 0) sN = 0;
        else if ((-d + b) > a) sN = sD;
        else { sN = (-d + b); sD = a; }
    }
    // finally do the division to get sc and tc
    float sc = (abs(sN) < __FLT_EPSILON__ ? 0 : sN / sD);
    float tc = (abs(tN) < __FLT_EPSILON__ ? 0 : tN / tD);
    A = a1 + (sc * u);
    B = b1 - (tc * v);
}

struct Contact {
    int index; // - wire, + particle (identify contact to avoid duplicates and evaluate friction)
    vec3 initialPosition; // Initial contact position
    int lastUpdate; // Time step of last update (to remove stale contacts)
    // Evaluate once to use both for normal and friction
    vec3 n; // Normal
    float fN; // Force
    vec3 v; // Relative velocity
    vec3 currentPosition; // Current contact position
    bool friction(vec3& fT, const float friction) const {
        //fT = 0; return false; // DEBUG
        const float staticFriction = friction;
        const float dynamicFriction = friction / 8;
        const float staticFrictionVelocity = 16;
        if(length(v) < staticFrictionVelocity) { // Static friction
            vec3 x = currentPosition - initialPosition;
            if(length(x) < 1./16) { // FIXME
                vec3 t = x-dot(n, x)*n;
                fT = - staticFriction * abs(fN) * t;
                //float l = length(t);
                //vec3 u = t/l;
                //if(l) fT -= particleTangentialDamping * dot(u, c.v) * u;
                // else static equilibrium
                return true; // Keep spring
            }
        }
        // Dynamic friction
        fT = - dynamicFriction * fN * v / length(v);
        return false; // Removes spring (readded every step for fully dynamic friction)
    }
};
bool operator==(const Contact& a, const Contact& b) { return a.index == b.index; }

struct DEM : Widget, Poll {
    struct Node {
        vec3 position;
        vec3 velocity = 0;
        vec3 force = 0; // Temporary for explicit integration
        array<Contact> contacts;
        bool valid = true;

        Node(vec3 position) : position(position) {}
    };

    struct Particle : Node {
        float radius;
        float mass;

        quat rotation {1, 0};
        vec3 angularVelocity = 0; // Global
        vec3 torque = 0; // Temporary for explicit integration

        Particle(vec3 position, float radius) : Node(position), radius(radius), mass(4./3*PI*cb(radius)) {}
    };

    array<Particle> particles;
    array<Node> nodes;

    struct Grid : buffer<uint16> {
        int3 size;
        Grid(int3 size) : buffer(size.z*size.y*size.x*8), size(size) { clear(); }
        struct List : mref<uint16> {
            List(mref<uint16> o) : mref(o) {}
            void remove(uint16 index) {
                size_t i = 0;
                while(at(i)) { if(at(i)==index) break; i++; }
                assert(i<8);
                while(i+1<8) { at(i) = at(i+1); i++; }
                assert(i<8);
                at(i) = 0;
            }
            void append(uint16 index) { // Sorted by decreasing index
                size_t i = 0;
                while(at(i) > index) i++;
                size_t j=i;
                while(index) { swap(index, at(j)); j++; }
                at(j) = 0;
            }
        };
        List operator[](size_t i) { return slice(i*8, 8); }
        size_t index(int x, int y, int z) {
            //assert_(x>=0 && y>=0 && z>=0, x,y,z);
            x = clamp(0, x, size.x-1);
            y = clamp(0, y, size.y-1);
            z = clamp(0, z, size.z-1);
            return (z*size[1]+y)*size[0]+x;
        }
        int3 index3(vec3 p) { // [-1..1, 0..2] -> [1..size-1]
            //assert_(p.x>=-1 && p.x<=1 && p.y>=-1 && p.y<=1 && p.z>=0 && p.z<=2, p);
            return int3(1,1,0) + int3(vec3(size-int3(2,2,1)) * (p+vec3(1,1,0))/2.f);
        }
        size_t index(vec3 p) { int3 i = index3(p); return index(i.x, i.y, i.z);}
    } particleGrid {32}, wireGrid {32};

    static constexpr float dt = 1./8192; //1e-7

    const float particleRadius = 1./16; // 62 mm
    static constexpr float particleYoung = 16;
    static constexpr float particleNormalDamping = 1, particleTangentialDamping = 1;
    const float particleFriction = 1;

    const float wireRadius = 1./128; // 8 mm
    static constexpr float wireYoung = 1;
    static constexpr float wireNormalDamping = 1;
    const float wireFriction = 1;
    //const float wireTangentialDamping = 0/*16*/;
    const float wireTensionStiffness = 1; // 16, 64, 1000
    const float wireTensionDamping = 1; // 2, 8
    const float wireBendStiffness = 1./128; // 2, 8, 100
    const float wireBendDamping = -1./128; // -8, -100
    static constexpr float internodeLength = 1./16;
    static constexpr float wireMass = 1./2048; //65536; //internodeLength * PI * wireRadius * wireRadius

    Random random;

    int timeStep = 0;
    float time = 0;

    int64 lastReport = realTime();
    Time totalTime {true}, solveTime;
    Time sphereTime, cylinderTime, particleIntegrationTime, wireIntegrationTime;

    const float winchRadius = 1-particleRadius-wireRadius;
    const float scaffoldRadius = 1-particleRadius;
    float winchAngle = 0, winchHeight = wireRadius;

    bool scaffold = true;

    void event() { step(); }
    void step() {
        if(scaffold) {
            if(particles.size<2048) { // Generates falling particle (pour)
                for(;;) {
                    vec3 p(random()*2-1,random()*2-1, min(2.f, winchHeight+particleRadius));
                    if(length(p.xy())>1) continue;
                    vec3 position ((scaffoldRadius-particleRadius)*p.xy(), p.z);
                    for(const Particle& p: particles) if(p.valid && length(p.position - position) < 2*particleRadius) goto break2_;
                    particles.append(position, particleRadius); // FIXME: reuse invalid slots
                    particleGrid[particleGrid.index(position)].append(particles.size-1);
                    break;
                }
                 break2_:;
            }
        }
        if(nodes.size<1024 && scaffold) { // Generates wire (winch)
            winchAngle += 256 * dt * winchRadius * internodeLength;
            winchHeight += 16 * dt * particleRadius/(winchRadius*2*PI); // Helix pitch
            vec3 winchPosition (winchRadius*cos(winchAngle), winchRadius*sin(winchAngle), winchHeight);
            vec3 r = winchPosition-nodes.last().position;
            float l = length(r);
            if(l > internodeLength*1.5) {
                if(nodes.size-1) wireGrid[wireGrid.index(nodes.last().position)].append(nodes.size-1);
                vec3 position = nodes.last().position + internodeLength/l*r;
                nodes.append(position);
            }
        } else scaffold = false;

        solveTime.start();

        // Initialization (force, torque)
        {constexpr vec3 g {0, 0, -1};
            for(Node& p: nodes)  p.force = wireMass * g;
            for(size_t index: range(particles.size)) {
                Particle& p = particles[index];
                if(!p.valid) continue;
                p.force = p.mass * g; p.torque = 0;
            }
        }

        // Sphere: gravity, contacts
        Lock lock;
        sphereTime.start();
        parallel_for(0, particles.size, [this, &lock](uint, size_t aIndex) {
            auto& a = particles[aIndex];
            if(!a.valid) { log("invalid", a.position); return; }

            // Sphere - Sphere
            auto contactSphereSphere = [this, &lock, &a](size_t bIndex) {
                auto& b = particles[bIndex];
                vec3 r = b.position - a.position;
                float l = length(r);
                float d = a.radius + b.radius - l;
                if(d <= 0) return;
                constexpr float E = particleYoung; ///2;
                float R = 1/(1/a.radius+1/b.radius);
                vec3 n = r/l;
                vec3 v = (a.velocity + cross(a.angularVelocity, a.radius*(+n))) - (b.velocity + cross(b.angularVelocity, b.radius*(-n)));
                float fN = - 4/3*E*sqrt(R)*sqrt(d)*d;
                vec3 fNormal = (fN -  particleNormalDamping * dot(n, v)) * n;
                a.force += fNormal;
                lock.lock();
                b.force -= fNormal;
                lock.unlock();
                {auto& c = a.contacts.add(Contact{int(bIndex), a.position+a.radius*n,0,0,0,0,0});
                    c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition=a.position+a.radius*n;}
            };
            // Sphere - Cylinder
            auto contactSphereCylinder = [this, &lock, &aIndex, &a](size_t i) {
                assert_(i+1 < nodes.size);
                vec3 A = nodes[i].position, B = nodes[i+1].position;
                float l = length(B-A);
                vec3 n = (B-A)/l;
                float t = clamp(0.f, dot(n, a.position-A), l);
                vec3 P = A + t*n;
                vec3 r = a.position - P;
                float d = a.radius + wireRadius - length(r);
                if(d > 0) {
                    vec3 n = r/length(r);
                    float E = 1/(1/particleYoung + 1/wireYoung);
                    float R = 1/(1/a.radius+1/wireRadius);
                    float fN = E*R*sqrt(d)*d; // FIXME
                    float kN = wireNormalDamping; //1/(1/wireNormalDamping + 1/particleNormalDamping);
                    vec3 v = (a.velocity + cross(a.angularVelocity, a.radius*(+n))) - (nodes[i].velocity+nodes[i+1].velocity)/2.f/*FIXME*/;
                    vec3 fNormal = (fN - kN * dot(n, v)) * n;
                    assert_(isNumber(fNormal));
                    a.force += fNormal;
                    lock.lock();
                    nodes[i].force -= fNormal /2.f;
                    nodes[i+1].force -= fNormal /2.f;
                    lock.unlock();
                    {
                        vec3 nA = a.position+a.radius*n;
                        vec3 nB = P         +wireRadius*n;
                        Contact& cA =             a.contacts.add(Contact{-int(i), nA,0,0,0,0,0});
                        lock.lock();
                        Contact& cB = nodes[i].contacts.add(Contact{int(aIndex), nB, 0,0,0,0,0});
                        lock.unlock();
                        cA.lastUpdate = timeStep; cA.n = -n; cA.fN = fN; cA.v = v; cA.currentPosition = nA;
                        cB.lastUpdate = timeStep; cB.n = n; cB.fN = fN; cB.v = -v; cB.currentPosition = nB;
                    }
                }
            };

            if(aIndex > 0) {
                if(a.position.z-particleRadius < 0) contactSphereSphere(0); // Floor
                int3 index = particleGrid.index3(a.position);
                // Sphere-Sphere
                for(int dz: range(-1, 1+1)) for(int dy: range(-1, 1+1)) for(int dx: range(-1, 1+1)) {
                    if(index.z+dz < 0) continue;
                    Grid::List list = particleGrid[particleGrid.index(index.x+dx, index.y+dy, index.z+dz)];
                    for(size_t i=0;; i++) {
                        size_t bIndex = list[i];
                        if(bIndex <= aIndex) break;
                        assert_(bIndex < particles.size, bIndex, particles.size);
                        contactSphereSphere(bIndex);
                    }
                }
                // Sphere-Cylinder
                for(int dz: range(-1, 1+1)) for(int dy: range(-1, 1+1)) for(int dx: range(-1, 1+1)) {
                    if(index.z+dz < 0) continue;
                    Grid::List list = wireGrid[wireGrid.index(index.x+dx, index.y+dy, index.z+dz)];
                    for(size_t i=0;; i++) {
                        size_t bIndex = list[i];
                        if(bIndex == 0) break;
                        assert_(bIndex+1 < nodes.size);
                        contactSphereCylinder(bIndex);
                    }
                }
            } else { // Floor
                for(size_t i: range(wireGrid.size.y*wireGrid.size.x)) {
                    Grid::List list = wireGrid[i];
                    for(size_t i=0;; i++) {
                        size_t bIndex = list[i];
                        if(bIndex == 0) break;
                        assert_(bIndex+1 < nodes.size);
                        contactSphereCylinder(bIndex);
                    }
                }
            }

            if(scaffold) { // Sphere - Scaffold
                vec3 r (a.position.xy(), 0);
                float l = length(r);
                float d = l - scaffoldRadius;
                if(d > 0) {
                    vec3 n = r/l;
                    float fN = - particleYoung*sqrt(d)*d;  //FIXME
                    a.force += fN * n;
                    vec3 v = (a.velocity + cross(a.angularVelocity, a.radius*(+n)));
                    a.force -= particleNormalDamping * dot(n, v) * n;
                    {auto& c = a.contacts.add(Contact{int(0), a.position+a.radius*n,0,0,0,0,0});
                        c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition=a.position+a.radius*n;}
                }
            }

            for(size_t i=0; i<a.contacts.size;) {
                const auto& c = a.contacts[i];
                if(c.lastUpdate != timeStep) { a.contacts.removeAt(i); continue; }
                vec3 fT;
                if(c.friction(fT, particleFriction)) i++; // Static
                else a.contacts.removeAt(i); // Dynamic
                a.force += fT;
                a.torque += cross(a.radius*c.n, fT);
            }

            //if(a.contacts) structureHeight = max(structureHeight, a.position.z); // Parallel fault
        });
        sphereTime.stop();
        // Wire tension
        for(int i: range(1,nodes.size-1)) {
            int a = i, b = i+1;
            vec3 A = nodes[a].position, B = nodes[b].position;
            float l = length(B-A);
            vec3 n = (B-A) / l;
            nodes[a].force += (wireTensionStiffness * (l-internodeLength) - wireTensionDamping * dot(n, nodes[a].velocity)) * n;
            nodes[b].force += (wireTensionStiffness * (internodeLength-l) - wireTensionDamping * dot(n, nodes[b].velocity)) * n;
        }
        cylinderTime.start();
        parallel_for(1, nodes.size-1, [this, &lock](uint, size_t i) {
            {// Torsion springs (Bending resistance)
                vec3 A = nodes[i-1].position, B = nodes[i].position, C = nodes[i+1].position;
                vec3 a = C-B, b = B-A;
                vec3 c = cross(a, b);
                float l = length(c);
                if(l) {
                    float p = atan(l, dot(a, b));
                    vec3 dap = cross(a, cross(a,b)) / (sq(a) * l);
                    vec3 dbp = cross(b, cross(b,a)) / (sq(b) * l);
                    lock.lock();
                    nodes[i+1].force += wireBendStiffness * (-p*dap);
                    nodes[i-1].force += wireBendStiffness * (p*dbp);
                    lock.unlock();
                    nodes[i].force += wireBendStiffness * (p*dap - p*dbp);
                    {
                        vec3 A = nodes[i-1].velocity, B = nodes[i].velocity, C = nodes[i+1].velocity;
                        vec3 axis = cross(C-B, B-A);
                        if(axis) {
                            float angularVelocity = atan(length(axis), dot(C-B, B-A));
                            nodes[i].force += (wireBendDamping * angularVelocity / 2.f * cross(axis/length(axis), C-A));
                        }
                    }
                }
            }

            // Cylinder - Cylinder
            auto contactCylinderCylinder = [this, &lock, i](int j) {
                vec3 A1 = nodes[i].position, A2 = nodes[i+1].position;
                vec3 B1 = nodes[j].position, B2 = nodes[j+1].position;
                vec3 A, B; closest(A1, A2, B1, B2, A, B);
                vec3 r = B-A;
                float d = wireRadius + wireRadius - length(r);
                if(d > 0) {
                    vec3 n = r/length(r);
                    vec3 v = (nodes[i].velocity + nodes[i+1].velocity)/2.f - (nodes[j].velocity + nodes[j+1].velocity)/2.f;
                    float fN = wireRadius/2*wireYoung/2*d*d*sqrt(d);
                    vec3 fNormal = (fN - wireNormalDamping/2 * dot(n, v)) * n;
                    assert_(isNumber(fNormal), i, j);
                    nodes[i].force += fNormal / 2.f;
                    lock.lock();
                    nodes[i+1].force += fNormal / 2.f;
                    nodes[j].force -= fNormal / 2.f;
                    nodes[j+1].force -= fNormal / 2.f;
                    lock.unlock();
                    {auto& c = nodes[i].contacts.add(Contact{-int(j), A+wireRadius*n,0,0,0,0,0});
                        c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition = A+wireRadius*n;
                    }
                }
            };
            size_t aIndex = i;
            int3 index = wireGrid.index3(nodes[aIndex].position);
            for(int dz: range(-1, 1+1)) for(int dy: range(-1, 1+1)) for(int dx: range(-1, 1+1)) {
                if(index.z+dz < 0) continue;
                Grid::List list = wireGrid[wireGrid.index(index.x+dx, index.y+dy, index.z+dz)];
                for(size_t i=0;; i++) {
                    size_t bIndex = list[i];
                    if(bIndex <= aIndex+1) break;
                    assert_(bIndex+1 < nodes.size, bIndex, nodes.size);
                    contactCylinderCylinder(bIndex);
                }
            }

            Node& a = nodes[i];
            if(scaffold)
            { // Cylinder - Scaffold FIXME
                vec3 r (a.position.xy(), 0);
                float l = length(r);
                float d = l - scaffoldRadius;
                if(d > 0) {
                    vec3 n = r/l;
                    float fN = - wireYoung*sqrt(d)*d;  //FIXME
                    a.force += fN * n;
                    vec3 v = a.velocity;
                    a.force -= wireNormalDamping * dot(n, v) * n;
                    {auto& c = a.contacts.add(Contact{int(0), a.position+wireRadius*n,0,0,0,0,0});
                        c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition=a.position+wireRadius*n;}
                }
            }

            for(size_t i=0; i<a.contacts.size;) {
                const auto& c = a.contacts[i];
                if(c.lastUpdate != timeStep) { a.contacts.removeAt(i); continue; }
                vec3 fT;
                if(c.friction(fT, wireFriction)) i++; // Static
                else a.contacts.removeAt(i); // Dynamic
                a.force += fT;
                assert_(isNumber(a.force));
            }
        });
        cylinderTime.stop();

        // Particle dynamics
        particleIntegrationTime.start();
        parallel_for(1, particles.size, [this, &lock](uint, size_t i) {
            Particle& p = particles[i];
            if(!p.valid) return;

            // Midpoint
            p.velocity += dt/2/wireMass*p.force;
            size_t oldCell = particleGrid.index(p.position);
            p.position += dt*p.velocity + dt*dt/2/wireMass*p.force;
            if(p.position.x <= -1 || p.position.x >= 1 || p.position.y <= -1 || p.position.y >= 1 || p.position.z <= 0 || p.position.z >= 2) {
                lock.lock();
                particleGrid[oldCell].remove(i);
                lock.unlock();
                p.valid = false;
                return;
            }
            size_t newCell = particleGrid.index(p.position);
            if(oldCell != newCell) {
                lock.lock();
                particleGrid[oldCell].remove(i);
                particleGrid[newCell].append(i);
                lock.unlock();
            }
            p.velocity += dt/2/wireMass*p.force;

            // PCDM rotation integration
            float I (2./3*p.mass*sq(p.radius)); // mat3
            vec3 w = (p.rotation.conjugate() * quat{0, p.angularVelocity} * p.rotation).v; // Local
            vec3 t = (p.rotation.conjugate() * quat{0, p.torque} * p.rotation).v; // Local
            vec3 dw = 1/I * (t - cross(w, I*w));
            vec3 w4 = w + dt/4*dw;
            float w4l = length(w4);
            // Prediction (multiplicative update)
            quat qp = w4l ? quat{cos(dt/4*w4l), sin(dt/4*w4l)*w4/w4l} * p.rotation : p.rotation;
            vec3 w2 = w + dt/2*dw;
            vec3 w2w = (qp * quat{0, w2} * qp.conjugate()).v; // Global
            float w2wl = length(w2w);
            // Correction (multiplicative update)
            p.rotation = w2wl ? quat{cos(dt/2*w2wl), sin(dt/2*w2wl)*w2w/length(w2w)} * p.rotation : p.rotation;
            p.angularVelocity = (p.rotation * quat{0, w + dt*dw} * p.rotation.conjugate()).v; // Global
        });
        particleIntegrationTime.stop();

        // Anchors both ends
        nodes.first().force = 0;
        nodes.last().force = 0;

        // Wire nodes dynamics
        wireIntegrationTime.start();
        parallel_for(1, nodes.size, [this, &lock](uint, size_t i) {
            Node& p = nodes[i];
            // Midpoint
            assert_(isNumber(p.force));
            p.velocity += dt/2 / wireMass * p.force;
            assert_(isNumber(p.velocity));
            size_t oldCell = wireGrid.index(p.position);
            p.position += dt*p.velocity + dt*dt/2/wireMass*p.force;
            assert_(isNumber(p.position));
            if(p.position.x <= -1 || p.position.x >= 1 || p.position.y <= -1 || p.position.y >= 1 || p.position.z >= 2) {
                lock.lock();
                wireGrid[oldCell].remove(i);
                lock.unlock();
                return;
            }
            size_t newCell = wireGrid.index(p.position);
            if(oldCell != newCell) {
                lock.lock();
                wireGrid[oldCell].remove(i);
                wireGrid[newCell].append(i);
                lock.unlock();
            }
            p.velocity += dt/2 / wireMass * p.force;
        });
        wireIntegrationTime.stop();
        solveTime.stop();
        time += dt;
        timeStep++;
        window->render();
        if(realTime() > lastReport+1e9) {
            log("solve",str(solveTime, totalTime), "sphere",str(sphereTime, solveTime), "cylinder",str(cylinderTime, solveTime),
                "particle integration", str(particleIntegrationTime, solveTime), "wire integration", str(wireIntegrationTime, solveTime));
            lastReport = realTime();
        }
        queue();
    }

    unique<Window> window = ::window(this, 1050);
    Thread solverThread;
    DEM() : Poll(0, POLLIN, solverThread) {
        window->actions[Space] = [this]{
            writeFile(str(time), encodePNG(render(1050, graphics(1050))), home());
        };
        window->actions[Return] = [this]{
            scaffold = false;
            window->setTitle("Release");
        };
        float floorRadius = 4096;
        particles.append(vec3(0,0,-floorRadius), floorRadius);
        nodes.append(vec3(winchRadius,0,wireRadius));
        step();
        solverThread.spawn();
        glDepthTest(true);
    }
    vec2 sizeHint(vec2) { return 1050; }
    shared<Graphics> graphics(vec2) {
        mat4 viewProjection = mat4() .scale(vec3(1, 1, -1./2)) .rotateX(rotation.y) .rotateZ(rotation.x); // Yaw

        if(particles.size > 1) {
            buffer<vec3> positions {(particles.size-1)*6};
            for(size_t i: range(particles.size-1)) {
                const auto& p = particles[1+i];
                // FIXME: GPU quad projection
                vec3 O = viewProjection*p.position;
                vec3 min = O - vec3(vec2(p.radius), 0); // Isometric
                vec3 max = O + vec3(vec2(p.radius), 0); // Isometric
                if(!p.valid) min=max=O;
                positions[i*6+0] = min;
                positions[i*6+1] = vec3(max.x, min.y, O.z);
                positions[i*6+2] = vec3(min.x, max.y, O.z);
                positions[i*6+3] = vec3(min.x, max.y, O.z);
                positions[i*6+4] = vec3(max.x, min.y, O.z);
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

        if(nodes.size > 1) {
            buffer<vec3> positions {(nodes.size-1)*6};
            for(int i: range(nodes.size-1)) {
                vec3 a = nodes[i].position, b = nodes[i+1].position;
                // FIXME: GPU quad projection
                vec3 A = viewProjection*a, B = viewProjection*b;
                vec2 r = B.xy()-A.xy();
                float l = length(r);
                vec2 t = r/l;
                vec3 n (t.y, -t.x, 0);
                float width = wireRadius;
                vec3 P[4] {A-width*n, A+width*n, B-width*n, B+width*n};
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
    vec2 rotation = vec2(0, -PI/2); // Current view angles (yaw,pitch)
    // Orbital ("turntable") view control
    bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        vec2 delta = cursor-lastPos; lastPos=cursor;
        if(event==Motion && button==LeftButton) {
            rotation += float(2.f*PI) * delta / size; //TODO: warp
            rotation.y= clamp(float(-PI/*2*/), rotation.y, 0.f); // Keep pitch between [-PI, 0]
        }
        else return false;
        return true;
    }
} view;
