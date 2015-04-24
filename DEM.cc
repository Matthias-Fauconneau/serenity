#include "thread.h"
#include "window.h"
#include "time.h"
#include "matrix.h"

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

    const float floorHeight = 1./2;
    const float floorYoung = 100000, floorPoisson = 0;
    const float floorNormalDamping = 10, floorTangentialDamping = 10;

    struct Particle {
        const float radius;
        const float mass;
        vec3 position;
        vec3 velocity = 0;
        vec3 acceleration; // Temporary
        quat rotation {1, 0};
        vec3 angularVelocity = 0; // Global
        vec3 torque; // Temporary
        Particle(vec3 position, float radius=1./16)
            : radius(radius), mass(1/**4./3*PI*cb(radius)*/), position(position) {}
    };
    array<Particle> particles;
    const float particleYoung = 1000, particlePoisson = 0;
    const float particleNormalDamping = 10, particleTangentialDamping = 1;

    void event() { step(); }
    void step() {
        bool generate = true;
        for(auto& p: particles) {
            if(p.position.z - p.radius < -1) generate = false;
        }
        if(generate && particles.size<1024) particles.append(vec3(random()*2-1,random()*2-1,-1));
        for(auto& p: particles) {
            vec3 force = 0;
            vec3 torque = 0; // Global
            // Gravity
            const vec3 g (0, 0, 0.1);
            force += p.mass * g;
            // Elastic sphere - half space contact
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
                float kT = floorTangentialDamping;
                const float smoothCoulomb = 0.1;
                float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                if(length(vT)) {
                    vec3 fT = (friction * abs(fN) * sC / length(vT) - kT) * vT;
                    force += fT;
                    torque += cross(p.radius*(-n), fT);
                }
                assert(isNumber(force));
            }
            for(const auto& b: particles) {
                if(&b == &p) continue;
                // Elastic sphere - sphere contact
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
            p.acceleration = force / p.mass;
            p.torque = torque;
        }
        for(auto& p: particles) {
            // Leapfrog position integration
            const float dt = 1./60;
            p.velocity += dt * p.acceleration;
            p.position += dt * p.velocity;
            assert(isNumber(p.position), p.position, p.velocity, force);
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
        window->render();
    }

    const float scale = 512;
    unique<Window> window = ::window(this, scale*2);
    DEM() {
        /*window->actions[Space] = [this]{
            writeFile(section(title,' '), encodePNG(render(512, graphics(512))), home());
        };*/
        window->presentComplete = {this, &DEM::step};
    }
    vec2 sizeHint(vec2) { return scale; }
    shared<Graphics> graphics(vec2 /*size*/) {
        shared<Graphics> graphics;

        // Computes view projection transform
        mat4 projection = mat4()
                .translate(vec3(scale,scale,0))
                .scale(scale);
                //.perspective(PI/4, size, 1, 2*horizonDistance + length(position));
        mat4 view = mat4()
                .rotateX(rotation.y) // Pitch
                .rotateZ(rotation.x) // Yaw
                ;//.translate(-position); // Position
        mat4 viewProjection = projection*view;

        graphics->lines.append( (viewProjection*vec3(-1, 0, floorHeight)).xy(),
                                (viewProjection*vec3(1, 0, floorHeight)).xy());
        graphics->lines.append( (viewProjection*vec3(0, -1, floorHeight)).xy(),
                                (viewProjection*vec3(0, 1, floorHeight)).xy());

        for(auto p: particles) {
            const int N = 24;
            for(int i: range(N)) { // TODO: Sphere
                float a = 2*PI*i/N;
                vec3 A = p.position + p.radius*vec3(cos(a),sin(a),0);
                float b = 2*PI*(i+1)/N;
                vec3 B = p.position + p.radius*vec3(cos(b),sin(b),0);
                graphics->lines.append((viewProjection*A).xy(), (viewProjection*B).xy());
            }
            vec3 al = p.radius*vec3(1,0,0);
            vec3 aw = p.position + (p.rotation * quat{0, al} * p.rotation.conjugate()).v;
            assert_(isNumber(aw), aw, p.position, p.rotation, al);
            graphics->lines.append((viewProjection*p.position).xy(), (viewProjection*aw).xy());
        }
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
