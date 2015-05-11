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
        fT = 0;
        return false;
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
        //Lock lock;
        vec3 position;
        vec3 velocity = 0;
        vec3 force = 0; // Temporary for explicit integration
        array<Contact> contacts;
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

    static constexpr float dt = 1./2048; //1e-7

    const float particleRadius = 1./4;
    static constexpr float particleYoung = 256;
    static constexpr float particleNormalDamping = 8, particleTangentialDamping = 1;
    const float particleFriction = 1;

    static constexpr float wireMass = 1./128; //1./256
    const float wireRadius = 1./16;
    static constexpr float wireYoung = 128;
    static constexpr float wireNormalDamping = 1./64 * wireMass / dt;
    const float wireFriction = 1;
    //const float wireTangentialDamping = 0/*16*/;
    const float wireTensionStiffness = 2; // 16, 64, 1000
    const float wireTensionDamping = 8; // 2, 8
    const float wireBendStiffness = 1./128; // 2, 8, 100
    const float wireBendDamping = -1; // -8, -100

    const float floorHeight = -1;

    float internodeLength = 1./4;

    array<Node> nodes;
    array<Particle> particles;

    Random random;

    int timeStep = 0;
    float time = 0;

    Time totalTime {true}, solveTime;
    Time sphereTime, cylinderTime;

    const float structureRadius = 1;
    float structureHeight = floorHeight;
    float pourHeight = floorHeight;
    float winchHeight = floorHeight;
    float winchAngle = 0;
    float lastPourWinchAngle = 0;

    void event() { step(); }
    void step() {
        particles.filter([](const Particle& p){ return length(p.position.xy()) > 2;});
        float height = floorHeight;
        for(const Particle& p: particles.slice(1)) height = max(height, p.position.z + p.radius);
        if(particles.size<128 && height < structureHeight+2*particleRadius) { // Generate falling particle (pouring)
            pourHeight = max(pourHeight, height+2*particleRadius);
            for(;;) {
                vec3 p(random()*2-1,random()*2-1, pourHeight);
                if(length(p.xy())>1) continue;
                particles.append(vec3((structureRadius-particleRadius)*p.xy(), p.z), particleRadius);
                break;
            }
        }
        if(1) { // Generate wire (spooling)
            winchAngle += 2 * dt * structureRadius*internodeLength;
            winchHeight += /*1./16 * exp(-time/256) **/ dt * particleRadius/(structureRadius*2*PI); // Helix pitch
            //winchHeight  = pourHeight;
            vec3 winchPosition (cos(winchAngle), sin(winchAngle), winchHeight);
            vec3 r = winchPosition-nodes.last().position;
            float l = length(r);
            if(l > internodeLength*1.5 // *2 to keep some tension
                    && nodes.size<256) nodes.append(nodes.last().position + internodeLength/l*r);
        }
        structureHeight = 0; Lock lock;

        solveTime.start();

        // Initialization (Gravity)
        {constexpr vec3 g {0, 0, -1};
            for(Node& p: nodes)  p.force = wireMass * g;
            for(Particle& p: particles)  { p.force = p.mass * g; p.torque = 0; }
        }

        // Sphere: gravity, contacts
        sphereTime.start();
        parallel_for(0, particles.size, [this, &lock](uint, size_t aIndex) {
            auto& a = particles[aIndex];

            // Sphere - Sphere
            for(size_t bIndex: range(particles.size)) { // Floor is particle #0
                if(aIndex == bIndex) continue;
                const auto& b = particles[bIndex];
                vec3 r = b.position - a.position;
                float l = length(r);
                float d = a.radius + b.radius - l;
                if(d <= 0) continue;
                constexpr float E = particleYoung/2;
                float R = 1/(1/a.radius+1/b.radius);
                vec3 n = r/l;
                vec3 v = (a.velocity + cross(a.angularVelocity, a.radius*(+n))) - (b.velocity + cross(b.angularVelocity, b.radius*(-n)));
                float fN = - 4/3*E*sqrt(R)*sqrt(d)*d - particleNormalDamping * dot(n, v);
                a.force += fN * n;
                {auto& c = a.contacts.add(Contact{int(bIndex), a.position+a.radius*n,0,0,0,0,0});
                    c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition=a.position+a.radius*n;}
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
                    float E = 1/(1/particleYoung + 1/wireYoung);
                    float kN = 1/(1/wireNormalDamping + 1/particleNormalDamping);
                    float R = 1/(1/a.radius+1/wireRadius);
                    vec3 n = r/length(r);
                    vec3 v = (a.velocity + cross(a.angularVelocity, a.radius*(+n))) - (nodes[i].velocity+nodes[i+1].velocity)/2.f/*FIXME*/;
                    float fN = 2*E*R*(d /*- wireRadius/4*/); // FIXME
                    a.force += fN * n;
                    a.force -= kN * dot(n, v) * n;
                    nodes[i].force -= fN / 2 * n;
                    nodes[i+1].force -= fN / 2 * n;
                    {
                        vec3 nA = a.position+a.radius*n;
                        vec3 nB = P         +wireRadius*n;
                        Contact& cA =             a.contacts.add(Contact{-int(i), nA,0,0,0,0,0});
                        Contact& cB = nodes[i].contacts.add(Contact{int(aIndex), nB, 0,0,0,0,0});
                        cA.lastUpdate = timeStep; cA.n = -n; cA.fN = fN; cA.v = v; cA.currentPosition = nA;
                        cB.lastUpdate = timeStep; cB.n = n; cB.fN = fN; cB.v = -v; cB.currentPosition = nB;
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
                if(c.friction(fT, particleFriction)) i++; // Static
                else a.contacts.removeAt(i); // Dynamic
                a.force += fT;
                a.torque += cross(a.radius*c.n, fT);
            }

            if(a.contacts) structureHeight = max(structureHeight, a.position.z); // Parallel fault
        });
        sphereTime.stop();
        // Wire tension
        if(nodes) for(int i: range(1,nodes.size-1)) {
            int a = i, b = i+1;
            vec3 A = nodes[a].position, B = nodes[b].position;
            float l = length(B-A);
            vec3 n = (B-A) / l;
            nodes[a].force += (wireTensionStiffness * (l-internodeLength) - wireTensionDamping * dot(n, nodes[a].velocity)) * n;
            nodes[b].force += (wireTensionStiffness * (internodeLength-l) - wireTensionDamping * dot(n, nodes[b].velocity)) * n;
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
                    nodes[n+1].force += wireBendStiffness * (-p*dap);
                    nodes[n].force += wireBendStiffness * (p*dap - p*dbp);
                    nodes[n-1].force += wireBendStiffness * (p*dbp);
                    {
                        vec3 A = nodes[n-1].velocity, B = nodes[n].velocity, C = nodes[n+1].velocity;
                        vec3 axis = cross(C-B, B-A);
                        if(axis) {
                            float angularVelocity = atan(length(axis), dot(C-B, B-A));
                            nodes[n].force += (wireBendDamping * angularVelocity / 2.f * cross(axis/length(axis), C-A));
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
                    vec3 n = r/length(r);
                    vec3 v = (nodes[i].velocity + nodes[i+1].velocity)/2.f - (nodes[j].velocity + nodes[j+1].velocity)/2.f;
                    float fN = wireRadius/2*wireYoung/2*d*d*sqrt(d);
                    force -= fN * n;
                    force -= wireNormalDamping/2 * dot(n, v) * n;
                    {auto& c = nodes[i].contacts.add(Contact{-int(j), A+wireRadius*n,0,0,0,0,0});
                        c.lastUpdate = timeStep; c.n = n; c.fN = fN; c.v = v; c.currentPosition = A+wireRadius*n;
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
                    if(c.friction(fT, wireFriction)) i++; // Static
                    else a.contacts.removeAt(i); // Dynamic
                    force += fT;
                }
            }

            //assert(isNumber(force));
            nodes[n].force += force / wireMass;
        });
        cylinderTime.stop();
        // Particle dynamics
        for(Particle& p: particles.slice(1)) {
            // Midpoint
            p.velocity += dt/2/wireMass*p.force;
            p.position += dt*p.velocity + dt*dt/2/wireMass*p.force;
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
        }
        if(nodes) {
            // Anchors both ends (explicit)
            nodes.first().force = 0;
            nodes.last().force = 0;
        }
        // Wire nodes dynamics
        for(Node& p: nodes) {
            // Midpoint
            p.velocity += dt/2 / wireMass * p.force;
            p.position += dt*p.velocity + dt*dt/2*p.force;
            p.velocity += dt/2 / wireMass * p.force;
        }
        solveTime.stop();
        time += dt;
        timeStep++;
        window->render();
        if(timeStep%(32*64)==0) log(/*"solve",str(solveTime, totalTime),*/
                                    "sphere",str(sphereTime, solveTime), "cylinder",str(cylinderTime, solveTime));
        queue();
    }

    const float scale = 1./2; //256;
    unique<Window> window = ::window(this, 1024);
    Thread solverThread;
    DEM() : Poll(0, POLLIN, solverThread) {
        window->actions[Space] = [this]{
            writeFile(str(time), encodePNG(render(1024, graphics(1024))), home());
        };
        float floorRadius = 4096;
        particles.append(vec3(0,0,-floorRadius+floorHeight), floorRadius);
        if(1) {
            nodes.append(vec3(1,0,floorHeight+wireRadius));
        } else if(0) { // Hanging wire
            const int N = 32;
            internodeLength = 3./N;
            for(int i: range(N)) {
                float x = float(i)/(N-1);
                nodes.append(vec3(x*2-1,0,-x));
            }
        }
        step();
        solverThread.spawn();
        glDepthTest(true);
    }
    vec2 sizeHint(vec2) { return 1024; }
    shared<Graphics> graphics(vec2) {
        mat4 viewProjection = mat4() .scale(vec3(scale, scale, -scale/4)) .rotateX(rotation.y) .rotateZ(rotation.x); // Yaw

        if(particles.size > 1) {
            buffer<vec3> positions {(particles.size-1)*6};
            for(size_t i: range(particles.size-1)) {
                const auto& p = particles[1+i];
                // FIXME: GPU quad projection
                vec3 O = viewProjection*p.position;
                vec3 min = O - vec3(vec2(scale*p.radius), 0); // Isometric
                vec3 max = O + vec3(vec2(scale*p.radius), 0); // Isometric
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
                float width = scale*wireRadius;
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
