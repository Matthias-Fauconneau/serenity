#include "thread.h"
#include "window.h"
#include "time.h"

// -> vector.h
template<template<Type> /*Type*/class V, Type T, uint N> inline /*constexpr*/ float length(const vec<V,T,N>& a) { return sqrt(dot(a,a)); }

struct DEM : Widget, Poll {
    Random random;

    const float floorHeight = 1./2;
    const float floorYoung = 100000, floorPoisson = 0;
    const float floorNormalDamping = 10, floorTangentialDamping = 10;

    struct Particle {
        float radius;
        vec3 position, velocity;
    };
    array<Particle> particles;
    const float particleYoung = 10000, particlePoisson = 0;
    const float particleNormalDamping = 10, particleTangentialDamping = 1;

    void event() { step(); }
    void step() {
        bool generate = true;
        for(auto& p: particles) {
            if(p.position.y - p.radius < -1) generate = false;
        }
        if(generate) {
            particles.append({1./16,vec3(random()*2-1,-1,0),0});
        }
        for(auto& p: particles) {
            vec3 force = 0, torque = 0;
            // Gravity
            const float g = 1;
            const float mass = 1;
            force.y += g * mass;
            // Elastic sphere - half space contact
            float d = (p.position.y + p.radius) - floorHeight;
            if(d > 0) {
                float E = 1/( (1-sq(floorPoisson))/floorYoung + (1-sq(particlePoisson))/particleYoung );
                float kN = floorNormalDamping; // 1/(1/floorDamping + 1/particleDamping)) ?
                vec3 v = p.velocity;
                vec3 n (0, -1, 0);
                float vN = dot(n, v);
                float fN = 4/3*E*sqrt(p.radius)*d*sqrt(d) - kN * vN;
                //assert(fN >= 0, fN);
                force += fN * n;
                vec3 vT = v - dot(n, v)*n;
                float friction = -1; // mu
                float kT = floorTangentialDamping;
                const float smoothCoulomb = 0.1;
                float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                if(length(vT)) {
                    float fT = friction * abs(fN) * sC / length(vT) - kT;
                    force += fT * vT;
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
                    vec3 v = p.velocity-b.velocity;
                    vec3 n = r/length(r);
                    float fN = 4/3*E*sqrt(R)*sqrt(d)*d - kN * dot(n, v);
                    force += fN * n;
                    /*float friction = 0; // mu
                    float kT = 0;//particleDamping;
                    vec2 t = vec2(n.y, -n.x); //cross(n);
                    float fT = friction * fN - kT * dot(t, p.velocity-b.velocity);
                    force += fT * t;*/
                    vec3 vT = v - dot(n, v)*n;
                    float friction = -1; // mu
                    float kT = particleTangentialDamping;
                    const float smoothCoulomb = 0.1;
                    float sC = 1- smoothCoulomb/(smoothCoulomb+sq(vT));
                    if(length(vT)) {
                        float fT = friction * abs(fN) * sC / length(vT) - kT;
                        force += fT * vT;
                    }
                    assert(isNumber(force));
                }
            }
            // Euler explicit integration
            const float dt = 1./60;
            p.velocity += dt * force / mass;
            p.position += dt * p.velocity;
            assert(isNumber(p.position), p.position, p.velocity, force);
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
        }
        return graphics;
    }
} view;
