#include "thread.h"
#include "window.h"
#include "time.h"
#include "matrix.h"
#include "render.h"
#include "algebra.h"
#include "png.h"
#include "time.h"
#include "parallel.h"

// -> vector.h
template<template<Type> /*Type*/class V, Type T, uint N> inline /*constexpr*/ float length(const vec<V,T,N>& a) { return sqrt(dot(a,a)); }

struct quat {
    float s; vec3 v;
    quat conjugate() { return {s, -v}; }
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
    vec3 position;
    int lastUpdate; // Time step of last update (to remove stale contacts)
    vec3 n; float fN; vec3 v; // Evaluate once to use both for normal and friction
    bool friction(vec3& fT, const vec3 A, const float friction) const {
        const float staticFriction = friction;
        const float dynamicFriction = friction / 8;
        const float staticFrictionVelocity = 16;
        if(length(v) < staticFrictionVelocity) { // Static friction
            vec3 x = A - position;
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
    const int skip = 128; //8 16
    //const float dt = 1./(60*2);
    const float dt = 1./(60*8); // 4

    const float floorHeight = 1;

    struct Particle {
        float radius;
        float mass;
        vec3 position;
        vec3 velocity = 0;
        vec3 acceleration; // Temporary for explicit integration
        quat rotation {1, 0};
        vec3 angularVelocity = 0; // Global
        vec3 torque; // Temporary for explicit integration
        array<Contact> contacts;
        Particle(vec3 position, float radius)
            : radius(radius), mass(4./3*PI*cb(radius)), position(position) {}
    };
    array<Particle> particles;
    const float particleRadius = 1./4;
    const float particleYoung = 1024, particlePoisson = 0;
    const float particleNormalDamping = 8, particleTangentialDamping = 1;
    const float particleFriction = 1;

    struct Node {
        vec3 position;
        vec3 velocity = 0;
        vec3 acceleration; // Temporary for explicit integration
        array<Contact> contacts;
        Node(vec3 position) : position(position) {}
    };
    const float nodeMass = 1./32;
    const float wireRadius = 1./16;
    const float wireYoung = 1024 /*1024*/, wirePoisson = 1./8;
    const float wireNormalDamping = 8;
    const float wireFriction = 8;
    //const float wireTangentialDamping = 0/*16*/;
    const float wireTensionStiffness = 2; // 16, 64, 1000
    const float wireTensionDamping = 2; // 2, 8
    const float wireBendStiffness = 1./16; // 2, 8, 100
    const float wireBendDamping = -2; // -8, -100

    float internodeLength = 1./4;

    array<Node> nodes;

    Random random;

    int timeStep = 0;
    float time = 0;

    Time totalTime {true}, solveTime, renderTime;
    Time sphereTime, cylinderTime;

    void event() { step(); }
    void step() {
        if(1) {
            if(1) { // Generate falling particle (pouring)
                bool generate = true;
                for(auto& p: particles.slice(1)) {
                    if(p.position.z - p.radius < -1) generate = false;
                }
                if(generate && particles.size<128) {
                    for(;;) {
                        vec3 p(random()*2-1,random()*2-1,-1);
                        //vec3 p = 0;
                        if(length(p.xy())>1) continue;
                        particles.append(vec3((1-particleRadius)*p.xy(), p.z), particleRadius);
                        break;
                    }
                }
            }
            if(1) { // Generate wire (spooling)
                const float spoolerRate = 1./8;
                const float spoolerSpeed = 1./16 * exp(-time/256);
                float spoolerAngle = 2*PI*spoolerRate*time;
                float spoolerHeight = floorHeight-wireRadius-spoolerSpeed*time;
                vec3 spoolerPosition (cos(spoolerAngle), sin(spoolerAngle), spoolerHeight);
                vec3 r = spoolerPosition-nodes.last().position;
                float l = length(r);
                if(l > internodeLength*1.5 // *2 to keep some tension
                        && nodes.size<256) {
                //if(nodes.last().position.z > spoolerPosition.z + 2*internodeLength) {
                //if(timeStepCount%(16*60) == 0 && l) {
                    //log(nodes.last().position.z, spoolerPosition.z, 2*internodeLength);
                    vec3 u = nodes.last().position-nodes[nodes.size-2].position;
                    //vec3 a(1/.128,1/.128,0);
                    //vec3 a(0,0,1./2);
                    vec3 a(0,0,0);
                    nodes.append(nodes.last().position + internodeLength*(a*u/length(u) + (vec3(1,1,1)-a)*r/l));
                }
            }
        }
        solveTime.start();
        //Matrix M (nodes.size*3, nodes.size*3); Vector b (nodes.size*3); // TODO: CG solve, +particles
        // Cylinder: initialization, gravity
        for(int i: range(nodes.size)) {
            Node& p = nodes[i];

            p.acceleration = 0;
            /*if(1) { // Implicit
                for(int c: range(3)) {
                    M(i*3+c, i*3+c) = nodeMass;
                    b[i*3+c] = 0;
                }
            }*/
            { // Gravity
                const vec3 g (0, 0, 1);
                vec3 force = nodeMass * g;
                /*if(1) { // Implicit
                    for(int c: range(3)) {
                        b[i*3+c] += dt * force[c];
                    }
                } else*/ { // Explicit
                    p.acceleration += force / nodeMass;
                }
            }
        }
        // Sphere: gravity, contacts
        sphereTime.start();
        parallel_for(particles.size, [this](uint, size_t aIndex) {
            auto& a = particles[aIndex];

            /*// Prediction
            a.position = a.correctPosition + dt*a.correctVelocity + dt*dt/2/2*a.acceleration;
            a.velocity = a.correctVelocity + dt/2*a.acceleration;*/

            vec3 force = 0;
            vec3 torque = 0; // Global
            // Gravity
            const vec3 g (0, 0, 1);
            force += a.mass * g;
            // Sphere - Sphere
            for(size_t bIndex: range(particles.size)) { // Floor is particle #0
                if(aIndex == bIndex) continue;
                const auto& b = particles[bIndex];
                vec3 r = b.position - a.position;
                float l = length(r);
                float d = a.radius + b.radius - l;
                if(d <= 0) continue;
                float E = 1/((1-sq(particlePoisson))/particleYoung + (1-sq(particlePoisson))/particleYoung);
                float R = 1/(1/a.radius+1/b.radius);
                vec3 n = r/l;
                vec3 v = (a.velocity + cross(a.angularVelocity, a.radius*(+n))) - (b.velocity + cross(b.angularVelocity, b.radius*(-n)));
                //vec3 v = a.velocity - b.velocity;
                float fN = - 4/3*E*sqrt(R)*sqrt(d)*d - particleNormalDamping * dot(n, v);
                assert_(isNumber(fN));
                force += fN * n;
                //assert_(isNumber(force), aIndex, a.angularVelocity);
                auto& c = a.contacts.add(Contact{int(bIndex), a.position+a.radius*n,0,0,0,0});
                c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v;
            }
            // Sphere - Cylinder
            if(nodes) for(size_t i: range(nodes.size-1)) {
                vec3 A = nodes[i].position, B = nodes[i+1].position;
                float l = length(B-A);
                vec3 n = (B-A)/l;
                float t = clamp(0.f, dot(n, a.position-A), l);
                vec3 P = A + t*n;
                vec3 r = a.position - P;
                float d = a.radius + wireRadius - length(r);
                if(d > 0) {
                    float E = 1/((1-sq(particlePoisson))/particleYoung + (1-sq(wirePoisson))/wireYoung);
                    float kN = 1/(1/wireNormalDamping + 1/particleNormalDamping);
                    float R = 1/(1/a.radius+1/wireRadius);
                    vec3 n = r/length(r);
                    vec3 v = (a.velocity + cross(a.angularVelocity, a.radius*(+n))) - (nodes[i].velocity+nodes[i+1].velocity)/2.f/*FIXME*/;
                    float fN = 2*E*R*(d /*- wireRadius/4*/); // FIXME
                    /*if(fN > 0)*/ force += fN * n;
                    //float fT = 2*E*R*d;
                    force -= kN * dot(n, v) * n;
                    nodes[i].acceleration -= (fN * n) / (2 * nodeMass);
                    nodes[i+1].acceleration -= (fN * n) / (2 * nodeMass);
                    {
                        vec3 nA = a.position+a.radius*n;
                        vec3 nB = P         +wireRadius*n;
                        Contact& cA =             a.contacts.add(Contact{-int(i), nA,0,0,0,0});
                        Contact& cB = nodes[i].contacts.add(Contact{int(aIndex), nB, 0,0,0,0});
                        cA.lastUpdate = timeStep; cA.n = -n; cA.fN = fN; cA.v = v;
                        cB.lastUpdate = timeStep; cB.n = n; cB.fN = fN; cB.v = -v;
                        /*// Spring origin are relative to middle point of objects centers (better static contact approximation for moving objects)
                        vec3 pM = (cA.position+cB.position)/2.f;
                        vec3 nM = (nA+nB)/2.f;
                        vec3 c = nM-pM;
                        cA.position += c; cB.position += c;*/
                    }
                }
            }

            for(size_t i=0; i<a.contacts.size;) {
                const auto& c = a.contacts[i];
                if(c.lastUpdate != timeStep) { a.contacts.removeAt(i); continue; }
                vec3 fT;
                if(c.friction(fT, a.position + a.radius*c.n, particleFriction)) i++; // Static
                else a.contacts.removeAt(i); // Dynamic
                assert(isNumber(fT));
                force += fT;
                torque += cross(a.radius*c.n, fT);
            }

            a.acceleration = force / a.mass;
            a.torque = torque;
        });
        sphereTime.stop();
        // Wire tension
        if(nodes) for(int i: range(1,nodes.size-1)) {
            /*if(1) { // Implicit
                auto spring = [&](int i, int j) {
                    vec3 x = nodes[i].position - nodes[j].position;
                    float l = length(x);
                    vec3 n = x/l;
                    vec3 v = nodes[i].velocity - nodes[j].velocity;
                    vec3 f = - wireTensionStiffness * (l - internodeLength) * n - wireTensionDamping * n * dot(n, v);
                    mat3 o = outer(n, n);
                    mat3 dxf = (1 - internodeLength/l)*(1 - o) + o;
                    mat3 dvf = - wireTensionDamping * o;
                    for(int c0: range(3)) {
                        for(int c1: range(3)) {
                            M(i*3+c0, i*3+c1) -= dt*dvf(c0, c1) + dt*dt*dxf(c0, c1);
                            M(j*3+c0, j*3+c1) += dt*dvf(c0, c1) + dt*dt*dxf(c0, c1);
                        }
                        b[i*3+c0] += dt*(  f[c0] + dt*dxf(c0, c0)*nodes[i].velocity[c0] );
                        b[j*3+c0] -= dt*( f[c0] + dt*dxf(c0, c0)*nodes[j].velocity[c0] );
                    }
                };
                spring(i-1, i);  spring(i, i+1);
            } else*/ {
                int a = i, b = i+1;
                vec3 A = nodes[a].position, B = nodes[b].position;
                float l = length(B-A);
                vec3 n = (B-A) / l;
                // FIXME: stiff wire (>KPa) requires implicit Euler
                nodes[a].acceleration +=
                        (+ wireTensionStiffness * (l-internodeLength)
                         - wireTensionDamping * dot(n, nodes[a].velocity))
                        / nodeMass * n;
                nodes[b].acceleration +=
                        (+ wireTensionStiffness * (internodeLength-l)
                         - wireTensionDamping * dot(n, nodes[b].velocity))
                        / nodeMass * n;
            }
        }
        cylinderTime.start();
        parallel_for(1, nodes.size-1, [this](uint, size_t n) {
            {// Torsion springs (Bending resistance) (explicit)
                vec3 A = nodes[n-1].position, B = nodes[n].position, C = nodes[n+1].position;
                vec3 a = C-B, b = B-A;
                vec3 c = cross(a, b);
                float l = length(c);
                if(l) {
                    float p = atan(l, dot(a, b));
                    vec3 dap = cross(a, cross(a,b)) / (sq(a) * l);
                    vec3 dbp = cross(b, cross(b,a)) / (sq(b) * l);
                    // Explicit
                    nodes[n+1].acceleration += wireBendStiffness * (-p*dap) / nodeMass;
                    nodes[n].acceleration += wireBendStiffness * (p*dap - p*dbp) / nodeMass;
                    nodes[n-1].acceleration += wireBendStiffness * (p*dbp) / nodeMass;
                    {//FIXME
                        vec3 A = nodes[n-1].velocity, B = nodes[n].velocity, C = nodes[n+1].velocity;
                        vec3 axis = cross(C-B, B-A);
                        if(axis) {
                            float angularVelocity = atan(length(axis), dot(C-B, B-A));
                            nodes[n].acceleration += (wireBendDamping * angularVelocity / 2.f * cross(axis/length(axis), C-A)) / nodeMass;
                        }
                    }
                }
            }

            vec3 force = 0;

            // Cylinder - Cylinder (explicit)
            auto contact = [this,&force](int i, int j) {
                vec3 A1 = nodes[i].position, A2 = nodes[i+1].position;
                vec3 B1 = nodes[j].position, B2 = nodes[j+1].position;
                vec3 A, B; closest(A1, A2, B1, B2, A, B);
                vec3 r = B-A;
                float d = wireRadius + wireRadius - length(r);
                if(d > 0) {
                    float E = 1/((1-sq(wirePoisson))/wireYoung + (1-sq(wirePoisson))/wireYoung);
                    float kN = wireNormalDamping;
                    float R = 1/(1/wireRadius+1/wireRadius);
                    vec3 n = r/length(r);
                    vec3 v = (nodes[i].velocity + nodes[i+1].velocity)/2.f - (nodes[j].velocity + nodes[j+1].velocity)/2.f;
                    float fN = 2*R*E*d*sqrt(d); // ?
                    force -= fN * n;
                    force -= kN * dot(n, v) * n;
                    {auto& c = nodes[i].contacts.add(Contact{-int(j), A+wireRadius*n,0,0,0,0});
                        c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v;
                    }
                }
            };
            for(int j: range(0, n-1)) contact(n, j);
            for(int j: range(n+2, nodes.size-1)) contact(n, j);

            {
                auto& a = nodes[n];
                for(size_t i=0; i<a.contacts.size;) {
                    const auto& c = a.contacts[i];
                    if(c.lastUpdate != timeStep) { a.contacts.removeAt(i); continue; }
                    vec3 fT;
                    if(c.friction(fT, a.position+wireRadius*c.n, wireFriction)) i++; // Static
                    else a.contacts.removeAt(i); // Dynamic
                    force += fT;
                }
            }

            assert(isNumber(force));
            nodes[n].acceleration += force / nodeMass;
        });
        cylinderTime.stop();
        // Particle dynamics
        for(Particle& p: particles.slice(1)) {
            if(0) { // Euler
                p.position += dt * p.velocity;
                p.velocity += dt * p.acceleration;
            } else if(0) { // Leapfrog
                p.velocity += dt * p.acceleration;
                p.position += dt * p.velocity;
            } else { // Midpoint
                p.velocity += dt/2*p.acceleration;
                p.position += dt*p.velocity + dt*dt/2*p.acceleration;
                p.velocity += dt/2*p.acceleration;
            }
            assert(isNumber(p.position));

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
        }
        if(nodes) {
            // Anchors both ends (explicit)
            nodes.first().acceleration = 0;
            nodes.last().acceleration = 0;
        }
        // Wire nodes dynamics
        for(Node& p: nodes) {
            if(0) { // Euler
                p.position += dt * p.velocity;
                p.velocity += dt * p.acceleration;
            } else if(0) { // Leapfrog
                p.velocity += dt * p.acceleration;
                p.position += dt * p.velocity;
            } else { // Midpoint
                p.velocity += dt/2*p.acceleration;
                p.position += dt*p.velocity + dt*dt/2*p.acceleration;
                p.velocity += dt/2*p.acceleration;
            }
            assert(isNumber(p.position));
        }
        /*if(1) {
            // Implicit Euler
            Vector dv = UMFPACK(M).solve(b);
            // Anchors both ends (implicit)
            for(int c: range(3)) dv[0             *3+c] = 0;
            for(int c: range(3)) dv[(nodes.size-1)*3+c] = 0;
            for(int i: range(nodes.size)) {
                for(int c: range(3)) {
                    assert(isNumber(dv[i*3+c]),"\n"_+str(M),"\n",b,"\n",dv);
                    Node& n = nodes[i];
                    n.velocity[c] += dv[i*3+c];
                    n.position[c] += dt*n.velocity[c];
                    assert(isNumber(n.position), n.position, n.velocity, n.acceleration);
                }
            }
        }*/
        solveTime.stop();
        time += dt;
        timeStep++;
        if(timeStep%skip == 0) window->render();
        if(timeStep%(32*64)==0) log("render",str(renderTime, totalTime),"solve",str(solveTime, totalTime),
                                    "sphere",str(sphereTime, solveTime), "cylinder",str(cylinderTime, solveTime));
        queue();
    }

    const float scale = 256;
    unique<Window> window = ::window(this, 1024);
    DEM() {
        window->actions[Space] = [this]{
            writeFile(str(time), encodePNG(render(1024, graphics(1024))), home());
        };
        float floorRadius = 4096;
        particles.append(vec3(0,0,floorHeight+floorRadius), floorRadius);
        if(1) {
            nodes.append(vec3(1,0,floorHeight-wireRadius));
        } else if(0) { // Hanging wire
            const int N = 32;
            internodeLength = 3./N;
            for(int i: range(N)) {
                float x = float(i)/(N-1);
                nodes.append(vec3(x*2-1,0,-x));
            }
        }
        step();
    }
    vec2 sizeHint(vec2) { return 1024; }
    shared<Graphics> graphics(vec2 size) {
        renderTime.start();
        Image target = Image( int2(size) );
        target.clear(0xFF);
        ImageF zBuffer = ImageF(int2(size));
        zBuffer.clear(-inf);

        // Computes view projection transform
        mat4 projection = mat4()
                .translate(vec3(size/2.f,0))
                .scale(scale);
                //.perspective(PI/4, size, 1, 2*horizonDistance + length(position));
        mat4 view = mat4()
                .rotateX(rotation.y) // Pitch
                .rotateZ(rotation.x) // Yaw
                ;//.translate(-position); // Position
        mat4 viewProjection = projection*view;

        line(target, (viewProjection*vec3(-1, 0, floorHeight)).xy(),
             (viewProjection*vec3(1, 0, floorHeight)).xy());
        line(target, (viewProjection*vec3(0, -1, floorHeight)).xy(),
             (viewProjection*vec3(0, 1, floorHeight)).xy());

        /*sort<Particle>([&viewProjection](const Particle& a, const Particle& b) -> bool {
            return (viewProjection*a.position).z < (viewProjection*b.position).z;
        }, particles);*/

        for(const auto& p: particles.slice(1)) {
            vec3 O = viewProjection*p.position;
            vec2 min = O.xy() - vec2(scale*p.radius); // Isometric
            vec2 max = O.xy() + vec2(scale*p.radius); // Isometric
            for(int y: range(::max(0.f, min.y), ::min(max.y, size.y))) {
                for(int x: range(::max(0.f, min.x+1), ::min(max.x+1, size.x))) {
                    vec2 R = vec2(x,y) - O.xy();
                    if(length(R)<scale*p.radius) {
                        vec2 r = R / (scale*p.radius);
                        vec3 N (r, 1-length(r)); // ?
                        float z = O.z;// + N.z;
                        if(z < zBuffer(x,y)) continue;
                        zBuffer(x,y) = z;
                        float I = /*::max(0.f,*/ dot(N, vec3(0,0,1));//);
                        if(I>=1) continue;
                        assert_(I<1);
                        extern uint8 sRGB_forward[0x1000];
                        target(x,y) = byte4(byte3(sRGB_forward[int(0xFFF*I)]),0xFF);
                        //assert_(N.x >= 0 && N.x < 1 && N.y >= 0 && N.y < 1 && N.z >= 0 && N.z < 1, N);
                        /*target(x,y) = byte4( // TODO: local coordinates
                                sRGB_forward[int(0xFFF*N.x)],
                                sRGB_forward[int(0xFFF*N.y)],
                                sRGB_forward[int(0xFFF*N.z)], 0xFF);*/
                    }
                }
            }
        }

        for(int i: range(nodes.size-1)) {
            vec3 a = nodes[i].position, b = nodes[i+1].position;
            vec3 A = viewProjection*a, B = viewProjection*b;
            vec2 r = B.xy()-A.xy();
            float l = length(r);
            vec2 t = r/l;
            vec2 n (t.y, -t.x);
            float width = scale*wireRadius;
            vec2 P[4] {A.xy()-width*n, A.xy()+width*n, B.xy()-width*n, B.xy()+width*n};
            vec2 min = ::min(P), max = ::max(P);
            for(int y: range(::max(0.f, min.y), ::min(max.y, size.y))) {
                for(int x: range(::max(0.f, min.x+1), ::min(max.x+1, size.x))) {
                    vec2 p = vec2(x,y) - A.xy();
                    if(dot(p,t) < 0 || dot(p,t) > l) continue;
                    if(dot(p,n) < -width || dot(p,n) > width) continue;
                    float z = A.z + dot(p,t)/l*(B.z-A.z);
                    float dz = 1-sq(dot(p,n)/width); // ?
                    z += width*dz;
                    if(z < zBuffer(x,y)) continue;
                    zBuffer(x,y) = z;
                    float I = dz;
                    assert_(I>=0 && I<=1, I, a, b);
                    extern uint8 sRGB_forward[0x1000];
                    target(x,y) = byte4(byte3(sRGB_forward[int(0xFFF*I)]),0xFF);
                }
            }
            if(0) {const auto& a = nodes[i];
                for(const auto& c: a.contacts) {
                    vec2 A = (viewProjection*(a.position+wireRadius*c.n)).xy();
                    vec2 B = (viewProjection*c.position).xy();
                    line(target, A, B, red);
                    if(length(B-A)) line(target, A, A+16.f*vec2((B-A).y,(B-A).x)/length(B-A), red);
                }
            }
        }

        if(0) for(const auto& a : particles) {
            for(const auto& c: a.contacts) {
                vec2 A = (viewProjection*(a.position+a.radius*c.n)).xy();
                vec2 B = (viewProjection*c.position).xy();
                line(target, A, B, red);
                if(length(B-A)) line(target, A, A+16.f*vec2((B-A).y,(B-A).x)/length(B-A), red);
            }
        }

        shared<Graphics> graphics;
        graphics->blits.append(vec2(0),vec2(target.size),move(target));
        renderTime.stop();
        return graphics;
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
