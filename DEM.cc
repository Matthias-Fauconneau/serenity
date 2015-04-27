#include "thread.h"
#include "window.h"
#include "time.h"
#include "matrix.h"
#include "render.h"

// -> vector.h
template<template<Type> /*Type*/class V, Type T, uint N> inline /*constexpr*/ float length(const vec<V,T,N>& a) { return sqrt(dot(a,a)); }

struct quat {
    float s; vec3 v;
    quat conjugate() { return {s, -v}; }
};
quat operator*(quat p, quat q) { return {p.s*q.s - dot(p.v, q.v), p.s*q.v + q.s*p.v + cross(p.v, q.v)}; }
String str(quat q) { return "["+str(q.s, q.v)+"]"; }

struct DEM : Widget, Poll {
    Random random;

    const float floorHeight = 1;
    const float floorYoung = 100000, floorPoisson = 0;
    const float floorNormalDamping = 10, floorTangentialDamping = 10;

    struct Particle {
        float radius;
        float mass;
        vec3 position;
        vec3 velocity = 0;
        vec3 acceleration; // Temporary
        quat rotation {1, 0};
        vec3 angularVelocity = 0; // Global
        vec3 torque; // Temporary
        Particle(vec3 position, float radius)
            : radius(radius), mass(1/**4./3*PI*cb(radius)*/), position(position) {}
    };
    array<Particle> particles;
    const float particleRadius = 1.f/4;
    const float particleYoung = 1000, particlePoisson = 0;
    const float particleNormalDamping = 10, particleTangentialDamping = 1;

    struct Node {
        vec3 position;
        vec3 velocity = 0;
        vec3 acceleration; // Temporary
        Node(vec3 position) : position(position) {}
    };
    const float nodeMass = 1;
    const float wireRadius = 1.f/16;
    const float wireYoung = 100, wirePoisson = 0;
    const float wireStiffness = 1;
    const float wireDamping = 1;
    float internodeLength;
    buffer<Node> nodes = [this](){
        const int N = 24;
        buffer<Node> nodes (N);
        for(int i: range(N)) {
            float a = 2*PI*i/N;
            nodes[i] = vec3(cos(a), sin(a), floorHeight+wireRadius);
        }
        internodeLength = length(nodes[1].position-nodes[0].position);
        /*for(int i: range(nodes.size)) {
            int a = i, b = (i+1)%nodes.size;
            vec3 A = nodes[a].position, B = nodes[b].position;
            float l = length(B-A);
            //if(internodeLength != l) error(str(internodeLength,8u), str(l,8u));
        }*/
        return nodes;
    }();

    void event() { step(); }
    void step() {
        bool generate = true;
        for(auto& p: particles) {
            if(p.position.z - p.radius < -1) generate = false;
        }
        if(generate && particles.size<1024) {
            for(;;) {
                vec3 p(random()*2-1,random()*2-1,-1);
                if(length(p.xy())>1) continue;
                particles.append(p, particleRadius);
                break;
            }
        }
        // Gravity, Wire - Floor contact
        for(Node& p: nodes) {
            vec3 force = 0;
            // Gravity
            const vec3 g (0, 0, 1);
            force += nodeMass * g;
            // Elastic sphere - Floor contact
            float d = (p.position.z + wireRadius) - floorHeight;
            if(d > 0) {
                float E = 1/( (1-sq(floorPoisson))/floorYoung + (1-sq(wirePoisson))/wireYoung );
                float kN = floorNormalDamping; // 1/(1/floorDamping + 1/wireDamping)) ?
                vec3 n (0, 0, -1);
                vec3 v = p.velocity;
                float vN = dot(n, v);
                float fN = 4/3*E*sqrt(wireRadius)*d*sqrt(d) - kN * vN; //FIXME
                force += fN * n;
                vec3 vT = v - dot(n, v)*n;
                float friction = -1; // mu
                float kT = floorTangentialDamping; // 1/(1/floorDamping + 1/wireDamping)) ?
                const float smoothCoulomb = 0.1;
                float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                if(length(vT)) {
                    vec3 fT = (friction * abs(fN) * sC / length(vT) - kT) * vT;
                    force += fT;
                }
                assert(isNumber(force));
            }
            p.acceleration = force / nodeMass;
        }
        for(auto& p: particles) {
            vec3 force = 0;
            vec3 torque = 0; // Global
            // Gravity
            const vec3 g (0, 0, 1);
            force += p.mass * g;
            // Elastic sphere - Floor contact
            float d = (p.position.z + p.radius) - floorHeight;
            if(d > 0) {
                float E = 1/( (1-sq(floorPoisson))/floorYoung + (1-sq(particlePoisson))/particleYoung );
                float kN = floorNormalDamping; // 1/(1/floorDamping + 1/particleDamping)) ?
                vec3 n (0, 0, -1);
                vec3 v = p.velocity + cross(p.angularVelocity, p.radius*(-n));
                float vN = dot(n, v);
                float fN = 4/3*E*sqrt(p.radius)*d*sqrt(d) - kN * vN;
                force += fN * n;
                vec3 vT = v - dot(n, v)*n;
                float friction = -1; // mu
                float kT = floorTangentialDamping; // 1/(1/floorDamping + 1/particleDamping)) ?
                const float smoothCoulomb = 0.1;
                float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                if(length(vT)) {
                    vec3 fT = (friction * abs(fN) * sC / length(vT) - kT) * vT;
                    force += fT;
                    torque += cross(p.radius*(-n), fT);
                }
                assert(isNumber(force));
            }
            // Elastic sphere - sphere contact
            for(const auto& b: particles) {
                if(&b == &p) continue;
                vec3 r = p.position - b.position;
                float d = p.radius + b.radius - length(r);
                if(d > 0) {
                    float E = 1/((1-sq(particlePoisson))/particleYoung + (1-sq(particlePoisson))/particleYoung);
                    float kN = particleNormalDamping;
                    float R = 1/(1/p.radius+1/b.radius);
                    vec3 n = r/length(r);
                    vec3 v = p.velocity + cross(p.angularVelocity, p.radius*(-n))
                           -(b.velocity + cross(b.angularVelocity, b.radius*(+n)));
                    float fN = 4/3*E*sqrt(R)*sqrt(d)*d - kN * dot(n, v);
                    force += fN * n;
                    vec3 vT = v - dot(n, v)*n;
                    float friction = -1; // mu
                    float kT = particleTangentialDamping;
                    const float smoothCoulomb = 0.1;
                    float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                    if(length(vT)) {
                        vec3 fT = (friction * abs(fN) * sC / length(vT) - kT) * vT;
                        force += fT;
                        torque += cross(p.radius*(-n), fT);
                    }
                    assert(isNumber(force));
                }
            }
            // Elastic sphere - cylinder contact
            for(int i: range(nodes.size-1)) {
                int a = i, b = i+1;
                vec3 A = nodes[a].position, B = nodes[b].position;
                float l = length(B-A);
                vec3 n = (B-A)/l;
                float t = dot(n, p.position-A);
                if(t < 0 || t > l) continue;
                vec3 P = A + t*n;
                vec3 r = p.position - P;
                float d = p.radius + wireRadius - length(r);
                if(d > 0) {
                    float E = 1/((1-sq(particlePoisson))/particleYoung + (1-sq(wirePoisson))/wireYoung);
                    float kN = particleNormalDamping; // 1/(1/wireDamping + 1/particleDamping)) ?
                    float R = 1/(1/p.radius+1/wireRadius);
                    vec3 n = r/length(r);
                    vec3 v = p.velocity + cross(p.angularVelocity, p.radius*(-n))
                           ;//FIXME -(b.velocity + cross(b.angularVelocity, b.radius*(+n)));
                    float fN = 4/3*E*sqrt(R)*sqrt(d)*d - kN * dot(n, v);
                    force += fN * n;
                    nodes[a].acceleration -= (fN * n) / (2 * nodeMass);
                    nodes[b].acceleration -= (fN * n) / (2 * nodeMass);
                    vec3 vT = v - dot(n, v)*n;
                    float friction = -1; // mu
                    float kT = particleTangentialDamping; // 1/(1/wireDamping + 1/particleDamping)) ?
                    const float smoothCoulomb = 0.1;
                    float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                    if(length(vT)) {
                        vec3 fT = (friction * abs(fN) * sC / length(vT) - kT) * vT;
                        force += fT;
                        nodes[a].acceleration -= fT / (2 * nodeMass);
                        nodes[b].acceleration -= fT / (2 * nodeMass);
                        torque += cross(p.radius*(-n), fT);
                    }
                    assert(isNumber(force));
                }
            }
            p.acceleration = force / p.mass;
            p.torque = torque;
        }
        // Wire tension (TODO: bend resistance)
        for(int i: range(nodes.size-1)) {
            int a = i, b = i+1;
            vec3 A = nodes[a].position, B = nodes[b].position;
            float l = length(B-A);
            vec3 n = (B-A)/l;
            nodes[a].acceleration +=
                    (wireStiffness * (l-internodeLength) - wireDamping * dot(n, nodes[a].velocity)) / nodeMass * n;
            nodes[b].acceleration +=
                    (wireStiffness * (internodeLength-l) - wireDamping * dot(n, nodes[b].velocity)) / nodeMass * n;
        }
        // Anchors both ends
        nodes.first().acceleration = 0;
        nodes.last().acceleration = 0;

        const float dt = 1./60;
        // Particle dynamics
        for(Particle& p: particles) {
            // Leapfrog position integration
            p.velocity += dt * p.acceleration;
            p.position += dt * p.velocity;
            assert(isNumber(p.position));
            // PCDM rotation integration
            /*mat3*/ float I (2./3*p.mass*sq(p.radius));
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
        // Wire nodes dynamics
        for(Node& n: nodes) {
            // Leapfrog position integration
            n.velocity += dt * n.acceleration;
            n.position += dt * n.velocity;
            assert(isNumber(n.position));
        }
        window->render();
    }

    const float scale = 256;
    unique<Window> window = ::window(this, 1024);
    DEM() {
        /*window->actions[Space] = [this]{
            writeFile(section(title,' '), encodePNG(render(512, graphics(512))), home());
        };*/
        window->presentComplete = {this, &DEM::step};
    }
    vec2 sizeHint(vec2) { return 1024; }
    shared<Graphics> graphics(vec2 size) {
        Image target = Image( int2(size) );
        target.clear(0xFF);

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

        // TODO: Z-Buffer
        sort<Particle>([&viewProjection](const Particle& a, const Particle& b) -> bool {
            return (viewProjection*a.position).z < (viewProjection*b.position).z;
        }, particles);

        for(auto p: particles) {
            /*for(int plane: range(3)) {
                const int N = 24;
                for(int i: range(N)) { // TODO: Sphere
                    float a = 2*PI*i/N;
                    vec3 A = p.radius * (vec3[]){vec3(cos(a),sin(a),0), vec3(0,cos(a),sin(a)), vec3(sin(a),0,cos(a))}[plane];
                    vec3 Aw = p.position + (p.rotation * quat{0, A} * p.rotation.conjugate()).v;

                    float b = 2*PI*(i+1)/N;
                    vec3 B = p.radius * (vec3[]){vec3(cos(b),sin(b),0), vec3(0,cos(b),sin(b)), vec3(sin(b),0,cos(b))}[plane];
                    vec3 Bw = p.position + (p.rotation * quat{0, B} * p.rotation.conjugate()).v;

                    graphics->lines.append((viewProjection*Aw).xy(), (viewProjection*Bw).xy());
                }
            }*/
            vec2 O = (viewProjection*p.position).xy();
            vec2 min = O - vec2(scale*p.radius); // Isometric
            vec2 max = O + vec2(scale*p.radius); // Isometric
            for(int y: range(::max(0.f, min.y), ::min(max.y, size.y))) {
                for(int x: range(::max(0.f, min.x+1), ::min(max.x+1, size.x))) {
                    vec2 R = vec2(x,y) - O;
                    if(length(R)<scale*p.radius) {
                        vec2 r = R / (scale*p.radius);
                        vec3 N (r, 1-length(r));
                        float I = ::max(0.f, dot(N, vec3(0,0,1)));
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

        for(int i: range(nodes.size)) {
            vec3 a = nodes[i].position, b = nodes[(i+1)%nodes.size].position;
            line(target, (viewProjection*a).xy(), (viewProjection*b).xy());
        }

        shared<Graphics> graphics;
        graphics->blits.append(vec2(0),vec2(target.size),move(target));
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
