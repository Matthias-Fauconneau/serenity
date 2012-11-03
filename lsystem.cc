/// TODO: L-System
#include "process.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "matrix.h"

/// Bracketed, deterministic, context-free L-system
struct LSystem : Widget {
    Window window __(this,int2(1050,1050),"Koch"_);
    static constexpr int nofCurve=15, maxLevel=20;
    int curve=0+debug(nofCurve-1), level=4;
    LSystem() {
        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Key(KP_Sub)).connect([this]{level=(level+maxLevel-1)%maxLevel; window.render();});
        window.localShortcut(Key(KP_Add)).connect([this]{level=(level+1)%maxLevel; window.render();});
        window.localShortcut(Key(LeftArrow)).connect([this]{curve=(curve+nofCurve-1)%nofCurve; window.render();});
        window.localShortcut(Key(RightArrow)).connect([this]{curve=(curve+1)%nofCurve; window.render();});
    }
    void render(int2, int2 window) override {
        this->window.setTitle(string("#"_+dec(curve)+"@"_+dec(level)));
        map<byte,ref<byte>> rules;
        string code;
        float angle;
        switch(curve) {
#define simple(i,a,axiom,generator) case i: angle=PI/a; rules['F']=generator ""_; code = string(axiom ""_); break
        simple(0,2,"-F", "F+F-F-F+F");
        simple(1,2,"F-F-F-F", "F-F+F+FF-F-F+F");
        simple(2,2,"F-F-F-F", "FF-F-F-F-F-F+F");
        simple(3,2,"F-F-F-F", "FF-F-F-F-FF");
        simple(4,2,"F-F-F-F", "FF-F--F-F");
        simple(5,3,"F--F--F", "F+F--F+F");
#define double(i,a,axiom,p1,r1,p2,r2) case i: angle=PI/a; rules[p1]=r1 ""_; rules[p2]=r2 ""_; code = string(axiom ""_); break
        double(6,3,"R", 'L',"R+L+R", 'R',"L-R-L"); // Sierpinski gasket
        double(7,2,"L", 'L',"L+R+", 'R',"-L-R"); // Dragon curve
        double(8,3,"L", 'L',"L+R++R-L--LL-R+", 'R',"-L+RR++R+L--L-R"); // Hexagonal Gosper curve
        // "Trees"
        simple(9,7,"F", "F[+F]F[-F]F");
        simple(10,9,"F", "F[+F]F[-F][F]");
        simple(11,8,"F", "FF-[-F+F+F]+[+F-F-F]");
        double(12,9,"X", 'X',"F[+X]F[-X]+X", 'F',"FF");
        double(13,7,"X", 'X',"F[+X][-X]FX", 'F',"FF");
        double(14,8,"X", 'X',"F-[[X]+X]+F[+FX]-X", 'F',"FF");
        }
        for(int unused i: range(level)) {
            string next;
            for(char c: code) {
                if(rules.contains(c)) next << rules[c];
                else next << c;
            }
            code = move(next);
        }
        struct State { vec2 position, heading; };
        array<State> stack;
        State state __(vec2(0,0), vec2(0,1));
        struct Line { vec2 a,b; };
        array<Line> lines;
        vec2 min=0,max=0;
        for(byte command : code) {
            /**/  if(command=='-') { state.heading=mat2(cos(-angle),-sin(-angle),sin(-angle),cos(-angle))*state.heading; }
            else if(command=='+') { state.heading=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*state.heading; }
            else if(command=='[') { stack << state; }
            else if(command==']') { state = stack.pop(); }
            else { // all other letters are "forward" (uppercase draws a line)
                vec2 next = state.position+state.heading;
                if(command>='A' && command<='Z') lines << Line __(state.position,next);
                state.position = next;
                min=::min(min,state.position);
                max=::max(max,state.position);
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
