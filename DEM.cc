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

struct Contact { // 16 w
    int index; // - wire, + particle (identify contact to avoid duplicates and evaluate friction)
    vec3 initialPosition; // Initial contact position
    int lastUpdate; // Time step of last update (to remove stale contacts)
    // Evaluate once to use both for normal and friction
    vec3 n; // Normal
    float fN; // Force
    vec3 v; // Relative velocity
    vec3 currentPosition; // Current contact position
    vec3 friction(const float friction) {
        //return 0; // DEBUG
        const float staticFriction = friction;
        const float dynamicFriction = friction / 8;
        const float staticFrictionVelocity = 16;
        if(length(v) < staticFrictionVelocity) { // Static friction
            vec3 x = currentPosition - initialPosition;
            if(length(x) < 1./16) { // FIXME
                vec3 t = x-dot(n, x)*n;
                //float l = length(t);
                //vec3 u = t/l;
                //if(l) fT -= particleTangentialDamping * dot(u, c.v) * u;
                // else static equilibrium
                return - staticFriction * abs(fN) * t;
            }
        }
        // Dynamic friction
        initialPosition = currentPosition;
        return - dynamicFriction * fN * v / length(v);
    }
};
bool operator==(const Contact& a, const Contact& b) { return a.index == b.index; }
String str(const Contact& c) { return str(c.index); }

struct DEM : Widget, Poll {
    struct Node {
        vec3 position;
        vec3 velocity = 0;
        vec3 force = 0; // Temporary for explicit integration
        static constexpr size_t capacity = 16;
        size_t contactCount = 0;
        Contact contacts[capacity];

        Node(vec3 position) : position(position) {}
    };

    struct Particle : Node { // 12+1x16+12
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
                assert(i < 8);
                size_t j=i;
                while(index) { swap(index, at(j)); j++; }
                at(j) = 0;
            }
        };
        List operator[](size_t i) { return slice(i*8, 8); }
        size_t index(int x, int y, int z) {
            x = clamp(0, x, size.x-1);
            y = clamp(0, y, size.y-1);
            z = clamp(0, z, size.z-1);
            return (z*size[1]+y)*size[0]+x;
        }
        int3 index3(vec3 p) { // [-1..1, 0..2] -> [1..size-1]
            return int3(1,1,0) + int3(vec3(size-int3(2,2,1)) * (p+vec3(1,1,0))/2.f);
        }
        size_t index(vec3 p) { int3 i = index3(p); return index(i.x, i.y, i.z);}
    } particleGrid {32}, wireGrid {32};

    static constexpr float dt = 1./8192; // 1e-7

    const float particleRadius = 1./16; // 62 mm
    static constexpr float particleYoung = 512;
    static constexpr float particleNormalDamping = 1, particleTangentialDamping = 1;
    const float particleFriction = 1;

    const float wireRadius = 1./128; // 8 mm
    static constexpr float wireYoung = 128;
    static constexpr float wireNormalDamping = 1./2;
    const float wireFriction = 1;
    //const float wireTangentialDamping = 0/*16*/;
    const float wireTensionStiffness = 1; // 16, 64, 1000
    const float wireTensionDamping = 1; // 2, 8
    const float wireBendStiffness = 1./512; // 2, 8, 100
    const float wireBendDamping = -1./512; // -8, -100
    static constexpr float internodeLength = 1./8;
    static constexpr float wireMass = 1./2048; //65536; //internodeLength * PI * wireRadius * wireRadius

    Random random;

    int timeStep = 0;
    float time = 0;

    float forceLimit = 256,  velocityLimit = 128;

    int64 lastReport = realTime();
    Time totalTime {true}, solveTime, floorTime;
    Time sphereTime, cylinderContactTime, cylinderFrictionTime, particleIntegrationTime, wireIntegrationTime;

    bool pour = true;
    float scaffoldRadius = 1./2;
    const float winchRadius = scaffoldRadius-wireRadius;
    const float pourRadius = winchRadius-wireRadius-particleRadius;
    float winchAngle = 0, winchHeight = particleRadius;

    void event() { step(); }

    Lock particleLocks [threadCount];
    Lock& particleLock(size_t index) { return particleLocks[(threadCount-1)*index/(particles.size-1)]; }
    Lock wireLocks [threadCount];
    Lock& wireLock(size_t index) { return wireLocks[(threadCount-1)*index/(nodes.size-1)]; }
    Lock gridLocks [threadCount];
    Lock& gridLock(size_t index) { return gridLocks[(threadCount-1)*index/(particleGrid.Ref::size-1)]; }

    Contact& addContact(Node& a, Contact contact) {
        for(Contact& c: mref<Contact>(a.contacts, a.contactCount)) if(c==contact) return c;
        assert_(a.contactCount < a.capacity, contact.index, a.contacts);
        Contact& c = a.contacts[a.contactCount];
        a.contactCount++;
        c = contact;
        return c;
    }

    // Sphere - Sphere
    void contactSphereSphere(size_t aIndex, size_t bIndex) {
        auto& a = particles[aIndex];
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
        {Locker lock (particleLock(aIndex));
            a.force += fNormal;
            auto& c = addContact(a, Contact{int(bIndex), a.position+a.radius*n,0,0,0,0,0});
            c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition=a.position+a.radius*n;
        }
        {Locker lock (particleLock(bIndex));
            b.force -= fNormal; }
    }
    // Sphere - Cylinder
    void contactSphereCylinder(size_t aIndex, size_t i) {
        assert_(aIndex < particles.size, aIndex, particles.size);
        auto& a = particles[aIndex];
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
            a.force += fNormal;
            {Locker lock (wireLock(i));
                nodes[i].force -= fNormal /2.f;
                /*vec3 nA = a.position+a.radius*n;
                Contact& cA = addContact(nodes[i], Contact{-int(i), nA,0,0,0,0,0});
                cA.lastUpdate = timeStep; cA.n = -n; cA.fN = fN; cA.v = v; cA.currentPosition = nA;*/
            }
            {Locker lock (wireLock(i+1));
                nodes[i+1].force -= fNormal /2.f; }
        }
    }
    // Cylinder - Cylinder
    void contactCylinderCylinder(int i, int j) {
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
            {Locker lock (wireLock(i));
                nodes[i].force += fNormal / 2.f;
                auto& c = addContact(nodes[i], Contact{j, A+wireRadius*n,0,0,0,0,0});
                c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition = A+wireRadius*n;
            }
            {Locker lock (wireLock(i+1));
                nodes[i+1].force += fNormal / 2.f;
            }
            {Locker lock (wireLock(j));
                nodes[j].force -= fNormal / 2.f;
            }
            {Locker lock (wireLock(j+1));
                nodes[j+1].force -= fNormal / 2.f;
            }
        }
    }

    void step() {
        if(pour && (winchHeight>=2 || particles.size>=2048 || nodes.size>=1024)) { pour = false; scaffoldRadius *= 2; }
        if(pour) {
            // Generates falling particle (pour)
            for(;;) {
                vec3 p(random()*2-1,random()*2-1, winchHeight+particleRadius);
                if(length(p.xy())>1) continue;
                vec3 position ((pourRadius-particleRadius)*p.xy(), p.z);
                for(const Particle& p: particles) if(length(p.position - position) < 2*particleRadius) goto break2_;
                particles.append(position, particleRadius); // FIXME: reuse invalid slots
                particleGrid[particleGrid.index(position)].append(particles.size-1);
                break;
            }
             break2_:;
            // Generates wire (winch)
            winchAngle += 512 * dt * winchRadius * internodeLength;
            winchHeight += 2 * particleRadius * dt;
            vec3 winchPosition (winchRadius*cos(winchAngle), winchRadius*sin(winchAngle), winchHeight);
            vec3 r = winchPosition-nodes.last().position;
            float l = length(r);
            if(l > internodeLength*1.5) {
                if(nodes.size-1) wireGrid[wireGrid.index(nodes.last().position)].append(nodes.size-1);
                vec3 position = nodes.last().position + internodeLength/l*r;
                nodes.append(position);
            }
        }

        // Initialization, gravity, boundary
        {constexpr vec3 g {0, 0, -1};
            for(Node& a: nodes) {
                a.force = wireMass * g;
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
                        /*{auto& c = addContact(a, Contact{int(0), a.position+wireRadius*n,0,0,0,0,0});
                            c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition=a.position+wireRadius*n;}*/
                    }
                }
            }
            for(size_t index: range(particles.size)) {
                Particle& a = particles[index];
                a.force = a.mass * g; a.torque = 0;

                // Sphere - Boundary
                vec3 r (a.position.xy(), 0);
                float l = length(r);
                float d = l - scaffoldRadius;
                if(d > 0) {
                    vec3 n = r/l;
                    float fN = - particleYoung*sqrt(d)*d;  //FIXME
                    a.force += fN * n;
                    vec3 v = (a.velocity + cross(a.angularVelocity, a.radius*(+n)));
                    a.force -= particleNormalDamping * dot(n, v) * n;
                    /*{auto& c = addContact(a, Contact{int(0), a.position+a.radius*n,0,0,0,0,0});
                        c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition=a.position+a.radius*n;}*/
                }
            }
        }

        solveTime.start();

        // Floor
        floorTime.start();
        for(size_t i: range(particleGrid.size.y*particleGrid.size.x)) {
            Grid::List list = particleGrid[i];
            for(size_t i=0;; i++) {
                size_t bIndex = list[i];
                if(bIndex == 0) break;
                contactSphereSphere(bIndex, 0);
            }
        }
        for(size_t i: range(wireGrid.size.y*wireGrid.size.x)) {
            Grid::List list = wireGrid[i];
            for(size_t i=0;; i++) {
                size_t bIndex = list[i];
                if(bIndex == 0) break;
                contactSphereCylinder(0, bIndex);
            }
        }
        floorTime.stop();

        // Sphere: gravity, contacts
        sphereTime.start();
        parallel_for(1, particles.size, [this](uint, size_t aIndex) {
            Particle& a = particles[aIndex];
            int3 index = particleGrid.index3(a.position);
            // Sphere-Sphere
            for(int dz: range(-1, 1+1)) for(int dy: range(-1, 1+1)) for(int dx: range(-1, 1+1)) {
                if(index.z+dz < 0) continue;
                Grid::List list = particleGrid[particleGrid.index(index.x+dx, index.y+dy, index.z+dz)];
                for(size_t i=0;; i++) {
                    size_t bIndex = list[i];
                    if(bIndex <= aIndex) break;
                    contactSphereSphere(aIndex, bIndex);
                }
            }
            // Sphere-Cylinder
            for(int dz: range(-1, 1+1)) for(int dy: range(-1, 1+1)) for(int dx: range(-1, 1+1)) {
                if(index.z+dz < 0) continue;
                Grid::List list = wireGrid[wireGrid.index(index.x+dx, index.y+dy, index.z+dz)];
                for(size_t i=0;; i++) {
                    size_t bIndex = list[i];
                    if(bIndex == 0) break;
                    contactSphereCylinder(aIndex, bIndex);
                }
            }
        });

        // Contact frictions
        parallel_for(1, particles.size, [this](uint, size_t aIndex) {
            Particle& a = particles[aIndex];
            for(size_t i=0; i<a.contactCount;) {
                Contact& c = a.contacts[i];
                if(c.lastUpdate != timeStep) {
                    for(size_t j: range(i, a.contactCount-1)) a.contacts[j]=a.contacts[j+1];
                    a.contactCount--;
                    continue;
                } else i++;
                vec3 fT = c.friction(particleFriction);
                {Locker lock (particleLock(aIndex));
                    a.force += fT;
                    a.torque += cross(a.radius*c.n, fT); }
                if(c.index > 0) { // Sphere - Sphere
                    size_t bIndex = c.index;
                    Particle& b = particles[bIndex];
                    assert_(aIndex != bIndex);
                    Locker lock (particleLock(bIndex));
                    b.force -= fT;
                    b.torque += cross(b.radius*-c.n, -fT);
                } else if(c.index < 0) { // Sphere - Cylinder
                    size_t bIndex = size_t(-c.index);
                    Node& b = nodes[bIndex];
                    Locker lock (wireLock(bIndex));
                    b.force -= fT;
                }
            }
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

        cylinderContactTime.start();
        parallel_for(1, nodes.size-1, [this](uint, size_t i) {
            {// Torsion springs (Bending resistance)
                vec3 A = nodes[i-1].position, B = nodes[i].position, C = nodes[i+1].position;
                vec3 a = C-B, b = B-A;
                vec3 c = cross(a, b);
                float l = length(c);
                if(l) {
                    float p = atan(l, dot(a, b));
                    vec3 dap = cross(a, cross(a,b)) / (sq(a) * l);
                    vec3 dbp = cross(b, cross(b,a)) / (sq(b) * l);
                    {Locker lock(wireLock(i+1));
                        nodes[i+1].force += wireBendStiffness * (-p*dap);}
                    {Locker lock(wireLock(i));
                        nodes[i].force += wireBendStiffness * (p*dap - p*dbp);}
                    {Locker lock(wireLock(i-1));
                        nodes[i-1].force += wireBendStiffness * (p*dbp);}
                    {
                        vec3 A = nodes[i-1].velocity, B = nodes[i].velocity, C = nodes[i+1].velocity;
                        vec3 axis = cross(C-B, B-A);
                        if(axis) {
                            float angularVelocity = atan(length(axis), dot(C-B, B-A));
                            {Locker lock(wireLock(i));
                                nodes[i].force += (wireBendDamping * angularVelocity / 2.f * cross(axis/length(axis), C-A));}
                        }
                    }
                }
            }
            // Cylinder - Cylinder
            size_t aIndex = i;
            int3 index = wireGrid.index3(nodes[aIndex].position);
            for(int dz: range(-1, 1+1)) for(int dy: range(-1, 1+1)) for(int dx: range(-1, 1+1)) {
                if(index.z+dz < 0) continue;
                Grid::List list = wireGrid[wireGrid.index(index.x+dx, index.y+dy, index.z+dz)];
                for(size_t i=0;; i++) {
                    size_t bIndex = list[i];
                    if(bIndex <= aIndex+1) break;
                    contactCylinderCylinder(aIndex, bIndex);
                }
            }
        });
        cylinderContactTime.stop();

        // Contact frictions
        cylinderFrictionTime.start();
        parallel_for(1, nodes.size-1, [this](uint, size_t i) {
            Node& a = nodes[i];
            for(size_t i=0; i<a.contactCount;) {
                Contact& c = a.contacts[i];
                if(c.lastUpdate != timeStep) {
                    for(size_t j: range(i, a.contactCount-1)) a.contacts[j]=a.contacts[j+1];
                    a.contactCount--;
                    continue;
                } else i++;
                vec3 fT = c.friction(particleFriction);
                a.force += fT;
                // Cylinder - Cylinder
                Locker lock (wireLock(c.index));
                nodes[c.index].force -= fT;
                // Cylinder - Sphere -> Sphere - Cylinder
            }
        });
        cylinderFrictionTime.stop();

        // Particle dynamics
        particleIntegrationTime.start();
        bool stop = false;
        parallel_for(1, particles.size, [this, &stop](uint, size_t i) {
            Particle& p = particles[i];

            if(length(p.velocity) > velocityLimit) { log("PV", p.velocity, p.velocity); stop=true; }
            if(length(p.force) > forceLimit) { log("PF", p.velocity, p.force); stop=true; }

            // Midpoint
            p.velocity += dt/2/wireMass*p.force;
            size_t oldCell = particleGrid.index(p.position);
            p.position += dt*p.velocity + dt*dt/2/wireMass*p.force;
            size_t newCell = particleGrid.index(p.position);
            if(oldCell != newCell) {
                {Locker lock(gridLock(oldCell));
                    particleGrid[oldCell].remove(i);}
                //if(p.position.x >= -1 && p.position.x <= 1 && p.position.y >= -1 && p.position.y <= 1)
                {Locker lock(gridLock(newCell));
                    particleGrid[newCell].append(i);}
                particleLocks[0].unlock();
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
        parallel_for(1, nodes.size, [this,&stop](uint, size_t i) {
            Node& p = nodes[i];

            if(length(p.velocity) > velocityLimit) { log("WV", p.velocity, p.velocity); stop=true; }
            if(length(p.force) > forceLimit) { log("WF", p.velocity, p.force); stop=true; }

            // Midpoint
            p.velocity += dt/2 / wireMass * p.force;
            size_t oldCell = wireGrid.index(p.position);
            p.position += dt*p.velocity + dt*dt/2/wireMass*p.force;
            size_t newCell = wireGrid.index(p.position);
            if(oldCell != newCell) {
                {Locker lock(gridLock(oldCell));
                            wireGrid[oldCell].remove(i);}
                //if(p.position.x >= -1 && p.position.x <= 1 && p.position.y >= -1 && p.position.y <= 1)
                {Locker lock(gridLock(newCell));
                    wireGrid[newCell].append(i);}
            }
            p.velocity += dt/2 / wireMass * p.force;
        });
        wireIntegrationTime.stop();
        solveTime.stop();
        time += dt;
        timeStep++;
        window->render();
        if(realTime() > lastReport+2e9) {
            log("solve",str(solveTime, totalTime), "sphere",str(sphereTime, solveTime), "cylinder",str(cylinderContactTime, solveTime),
                 "cylinder",str(cylinderFrictionTime, solveTime), "floor",str(floorTime, solveTime),
                "particle integration", str(particleIntegrationTime, solveTime), "wire integration", str(wireIntegrationTime, solveTime));
            lastReport = realTime();
        }
        if(!stop) queue();
    }

    unique<Window> window = ::window(this, 1050);
    Thread solverThread;
    DEM() : Poll(0, POLLIN, solverThread) {
        //window->actions[Space] = [this]{ writeFile(str(time), encodePNG(render(1050, graphics(1050))), home()); };
        window->actions[Space] = [this]{ velocityLimit*=2; forceLimit*=2; queue(); solverThread.spawn(); };
        window->actions[Return] = [this]{
            if(pour) { pour = false; scaffoldRadius *= 2; window->setTitle("Release"); }
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
        mat4 viewProjection = mat4() .scale(vec3(1, 1, -1./2)) .rotateX(rotation.y) .rotateZ(rotation.x) .translate(vec3(0,0, -1));

        if(particles.size > 1) {
            buffer<vec3> positions {(particles.size-1)*6};
            for(size_t i: range(particles.size-1)) {
                const auto& p = particles[1+i];
                // FIXME: GPU quad projection
                vec3 O = viewProjection*p.position;
                vec3 min = O - vec3(vec2(p.radius), 0); // Isometric
                vec3 max = O + vec3(vec2(p.radius), 0); // Isometric
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
