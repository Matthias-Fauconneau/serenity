#include "thread.h"
#include "window.h"

struct DEM : Widget, Poll {
    struct Particle {
        float radius;
        vec2 position, velocity;
    };
    array<Particle> particles;

    void event() { step(); }
    void step() {
        for(auto& p: particles) {
            vec2 force = 0;
            // Gravity
            const float g = 1;
            const float mass = 1;
            force.y += g * mass;
            // Elastic sphere - half space contact
            float d = (p.position.y + p.radius)-0;
            if(d > 0) {
                float E = 100000; // 1/((1-sq(nu1))/E1 + (1-sq(nu2))/E2))
                float k = 10;
                float F = -/*normal toward -*/ 4/3*E*sqrt(p.radius)*d*sqrt(d) - k*p.velocity.y;
                force.y += F;
            }
            // Euler explicit integration
            const float dt = 1./60;
            p.velocity += dt * force / mass;
            p.position += dt * p.velocity;
            assert(isNumber(p.position), p.position);
        }

        window->render();
    }

    const float scale = 512;
    unique<Window> window = ::window(this, scale*2);
    DEM() {
        particles.append({1./16,vec2(0,-1),0});

        /*window->actions[Space] = [this]{
            writeFile(section(title,' '), encodePNG(render(512, graphics(512))), home());
        };*/
        window->presentComplete = {this, &DEM::step};
    }
    vec2 sizeHint(vec2) { return scale; }
    shared<Graphics> graphics(vec2 /*size*/) {
        vec2 offset = scale;
        shared<Graphics> graphics;
        graphics->lines.append(scale*vec2(-1,0)+offset, scale*vec2(1,0)+offset);
        for(auto p: particles) {
            const int N = 24;
            for(int i: range(N)) {
                float a = 2*PI*i/N;
                vec2 A = scale * (p.position + p.radius*vec2(cos(a),sin(a))) + offset;
                float b = 2*PI*(i+1)/N;
                vec2 B = scale * (p.position + p.radius*vec2(cos(b),sin(b))) + offset;
                graphics->lines.append(A, B);
            }
        }
        return graphics;
    }
} view;
