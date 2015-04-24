#include "thread.h"
#include "window.h"
#include "time.h"
//include "matrix.h"

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
        quat rotation {1, 0};
        vec3 angularVelocity = 0; // Global
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
            if(p.position.y - p.radius < -1) generate = false;
        }
        if(generate && particles.size<1024) particles.append(vec3(random()*2-1,-1,0));
        for(auto& p: particles) {
            vec3 force = 0;
            vec3 torque = 0; // Global
            // Gravity
            const vec3 g (0.0, 0.1, 0);
            force += p.mass * g;
            // Elastic sphere - half space contact
            float d = (p.position.y + p.radius) - floorHeight;
            if(d > 0) {
                float E = 1/( (1-sq(floorPoisson))/floorYoung + (1-sq(particlePoisson))/particleYoung );
                float kN = floorNormalDamping; // 1/(1/floorDamping + 1/particleDamping)) ?
                vec3 n (0, -1, 0);
                vec3 v = p.velocity + cross(p.angularVelocity, vec3(0, p.radius, 0));
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
                    vec3 v = p.velocity + cross(p.angularVelocity, vec3(0, p.radius, 0))
                           -(b.velocity + cross(b.angularVelocity, vec3(0, b.radius, 0)));
                    vec3 n = r/length(r);
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
            // Euler position integration (TODO: Velocity verlet)
            const float dt = 1./60;
            p.velocity += dt * force / p.mass;
            p.position += dt * p.velocity;
            assert(isNumber(p.position), p.position, p.velocity, force);
            // PCDM rotation integration
            /*mat3*/ float I (2./3*p.mass*sq(p.radius));
            vec3 w = (p.rotation.conjugate() * quat{0, p.angularVelocity} * p.rotation).v; // Local
            vec3 t = (p.rotation.conjugate() * quat{0, torque} * p.rotation).v; // Local
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
        vec2 offset = scale;
        shared<Graphics> graphics;
        graphics->lines.append(scale*vec2(-1, floorHeight)+offset, scale*vec2(1, floorHeight)+offset);
        for(auto p: particles) {
            const int N = 24;
            for(int i: range(N)) {
                float a = 2*PI*i/N;
                vec2 A = scale * (p.position.xy() + p.radius*vec2(cos(a),sin(a))) + offset;
                float b = 2*PI*(i+1)/N;
                vec2 B = scale * (p.position.xy() + p.radius*vec2(cos(b),sin(b))) + offset;
                graphics->lines.append(A, B);
            }
            vec3 al = p.radius*vec3(1,0,0);
            vec3 aw = p.position + (p.rotation * quat{0, al} * p.rotation.conjugate()).v;
            assert_(isNumber(aw), aw, p.position, p.rotation, al);
            graphics->lines.append(scale*p.position.xy()+offset, scale*aw.xy()+offset);
        }
        return graphics;
    }
} view;
