/// TODO: L-System
#include "process.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "matrix.h"

/// Deterministic, context-free L-System (DOL)
struct LSystem : Widget {
    Window window __(this,int2(1024,1024),"Koch"_);
    LSystem() {
        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Key(KP_Sub)).connect([this]{level=(level+20-1)%20; window.render();});
        window.localShortcut(Key(KP_Add)).connect([this]{level=(level+1)%20; window.render();});
        window.localShortcut(Key(LeftArrow)).connect([this]{curve=(curve+8-1)%8; window.render();});
        window.localShortcut(Key(RightArrow)).connect([this]{curve=(curve+1)%8; window.render();});
    }
    int curve=0, level=4;
    void render(int2, int2 window) override {
        this->window.setTitle(string("#"_+dec(curve)+"@"_+dec(level)));
        map<byte,ref<byte>> rules;
        string code;
        switch(curve) {
#define koch(i,generator,axiom) case i: rules['F']=generator ""_; code = string(axiom ""_); break
        koch(0,"F+F-F-F+F","-F");
        koch(1,"F-F+F+FF-F-F+F","F-F-F-F");
        koch(2,"FF-F-F-F-F-F+F","F-F-F-F");
        koch(3,"FF-F-F-F-FF","F-F-F-F");
        koch(4,"FF-F--F-F","F-F-F-F");
        koch(5,"FrFllFrF","FllFllF");
        case 6: { //Sierpinski gasket
            rules['L']="RrLrR"_;
            rules['R']="LlRlL"_;
            code = string("R"_);
        } break;
        case 7: { //Dragon curve
            rules['L']="L+R+"_;
            rules['R']="-L-R"_;
            code = string("L"_);
        } break;
        }
        for(int unused i: range(level)) {
            string next;
            for(char c: code) {
                if(rules.contains(c)) next << rules[c];
                else next << c;
            }
            code = move(next);
        }
        vec2 position = vec2(0,0); vec2 heading = vec2(0,1);
        float step=1, angle=PI/2;
        struct Line { vec2 a,b; };
        array<Line> lines;
        vec2 min=0,max=0;
        for(byte command : code) {
            /**/  if(command=='-') { heading=mat2(cos(-angle),-sin(-angle),sin(-angle),cos(-angle))*heading; }
            else if(command=='+') { heading=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*heading; }
            else if(command=='l') { heading=mat2(cos(-PI/3),-sin(-PI/3),sin(-PI/3),cos(-PI/3))*heading; }
            else if(command=='r') { heading=mat2(cos(PI/3),-sin(PI/3),sin(PI/3),cos(PI/3))*heading; }
            else { // all other letters are "forward" (uppercase draws a line)
                vec2 next = position+step*heading;
                if(command>='A' && command<='Z') lines << Line __(position,next);
                position = next;
                min=::min(min,position);
                max=::max(max,position);
            }
        }
        mat3 m;
        vec2 size = max-min;
        float scale = ::min(window.x,window.y)/::max(size.x,size.y);
        m.scale(scale);
        vec2 margin = vec2(window)/scale-size;
        m.translate(-min+margin/2.f);
        for(Line line: lines) {
            vec2 a=m*line.a, b=m*line.b;
            ::line(a.x,window.y-a.y,b.x,window.y-b.y);
            //::line(round(a.x),round(window.y-a.y),round(b.x),round(window.y-b.y));
        }
    }
} application;
