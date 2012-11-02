/// TODO: L-System
#include "process.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "matrix.h"

/// Renders an L-System (TODO)
struct LSystem : Widget {
    Window window __(this,int2(1024,1024),"LSystem"_);
    int max=0;
    LSystem() {
        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Key(' ')).connect([this]{max=(max+1)%8; window.render();});
    }
    void render(int2 position, int2 size) override {
        window.setTitle(dec(max));
        array<vec2> polygon, generator;
        if(0) { //Triangle Koch
            polygon
                    << vec2(cos(1*PI/6),sin(1*PI/6))
                    << vec2(cos(5*PI/6),sin(5*PI/6))
                    << vec2(cos(9*PI/6),sin(9*PI/6))
                    << vec2(cos(1*PI/6),sin(1*PI/6));
            generator << vec2(0,0) << vec2(1./3,0) << vec2(1./3+cos(PI/3)/3,-sin(PI/3)/3) << vec2(2./3,0) << vec2(1,0);
        } else { //Quadratic Koch
            polygon << vec2(-1,0) << vec2(1,0);
            generator << vec2(0,0) << vec2(1./4,0) << vec2(1./4,1./4) << vec2(2./4,1./4) << vec2(2./4,0)
                      << vec2(2./4,-1./4) << vec2(3./4,-1./4) << vec2(3./4,0) << vec2(1,0);
        }
        for(int unused i: range(max)) {
            array<vec2> next;
            vec2 A=polygon.first();
            for(vec2 B : polygon.slice(1)) {
                for(vec2 p : generator) {
                    next << A+p.x*(B-A)+p.y*normal(B-A);
                }
                A=B;
            }
            polygon = move(next);
        }
        //polygon = copy(generator);
        vec2 A=polygon.first();
        mat3 m; m.translate(vec2(position+size/2)); m.scale(vec2(size/2));
        for(vec2 B: polygon.slice(1)) {
            line(m*A,m*B);
            A=B;
        }
    }
} application;
