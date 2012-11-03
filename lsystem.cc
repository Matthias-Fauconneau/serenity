// TODO: context sensitive, 3D, wide lines, polygons, instances, navigation
#include "process.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "matrix.h"
#include "time.h"

/// Generates a sequence of uniformly distributed pseudo-random 64bit integers
struct Random {
    uint64 sz,sw;
    uint64 z,w;
    Random() { seed(); reset(); }
    void seed() { sz=rdtsc(); sw=rdtsc(); }
    void reset() { z=sz; w=sw; }
    uint64 next() {
        z = 36969 * (z & 0xFFFF) + (z >> 16);
        w = 18000 * (w & 0xFFFF) + (w >> 16);
        return (z << 16) + w;
    }
    uint64 operator()() { return next(); }
} random;

/// Bracketed, deterministic, context-free L-system
struct LSystem : Widget {
    struct Rule {
        byte edge; ref<byte> production;
        ref<byte> left,right; //context
        Rule(byte edge, ref<byte> production, ref<byte> left=""_, ref<byte> right=""_):edge(edge),production(production),left(left),right(right){}
    };
    struct System {
       float angle;
       ref<byte> axiom;
       array<Rule> rules;
       System(float angle, ref<byte> axiom):angle(angle),axiom(axiom){}
       template<class... Args> System(float angle, ref<byte> axiom, Rule rule, Args... args):System(angle,axiom,args ___){rules<<rule;}
       array<byte> generate(int level) const {
           array<byte> code = string(axiom);
           for(int unused i: range(level)) {
               array<byte> next;
               for(uint i: range(code.size())) { byte c = code[i];
                   array<ref<byte>> matches; bool sensitive=false;
                   for(Rule r: rules) {
                       //TODO: context sensitive matches
                       if(r.edge==c) {
                           if(r.left || r.right) {
                               if(!sensitive) matches.clear(), sensitive=true;
                               if(r.left) {
                                   array<byte> path;
                                   for(int j=i-1; path.size()<r.left.size && j>=0; j--) {
                                       byte c = code[j];
                                       if(c==']') { while(code[j]!='[') j--; continue; } // skip brackets
                                       if(c=='+' || c=='-' || c=='[') continue;
                                       path << c;
                                   }
                                   if(path!=r.left) continue;
                               }
                               if(r.right) {
                                   array<byte> path;
                                   for(uint j=i+1; path.size()<r.right.size && j<code.size(); j++) {
                                       byte c = code[j];
                                       if(c=='[') { while(code[j]!=']') j++; continue; } // skip brackets
                                       if(c=='+' || c=='-') continue;
                                       path << c;
                                   }
                                   if(path!=r.right) continue;
                               }
                           } else if(sensitive) continue;
                           matches << r.production;
                       }
                   }
                   if(matches) next << matches[random()%matches.size()];
                   else next << c;
               }
               code = move(next);
           }
           return code;
       }
    };
    array<System> systems;

    Window window __(this,int2(0,0),"L-System"_);
    uint current=0, level=0; bool label=true;

    LSystem() {
        systems <<
                // Abstract curves
                   System(PI/2,"-F"_, Rule('F',"F+F-F-F+F"_)) <<
                   System(PI/2,"F-F-F-F"_, Rule('F',"F-F+F+FF-F-F+F"_)) <<
                   System(PI/2,"F-F-F-F"_, Rule('F',"FF-F-F-F-F-F+F"_)) <<
                   System(PI/2,"F-F-F-F"_, Rule('F',"FF-F-F-F-FF"_)) <<
                   System(PI/2,"F-F-F-F"_, Rule('F',"FF-F--F-F"_)) <<
                   System(PI/3,"F--F--F"_, Rule('F',"F+F--F+F"_)) <<
                   System(PI/3,"R"_, Rule('L',"R+L+R"_), Rule('R',"L-R-L"_)) << // Sierpinski gasket
                   System(PI/2,"L"_, Rule('L',"L+R+"_), Rule('R',"-L-R"_)) << // Dragon curve
                   System(PI/3,"L"_, Rule('L',"L+R++R-L--LL-R+"_), Rule('R',"-L+RR++R+L--L-R"_)) << // Hexagonal Gosper curve
                   // Plants
                   System(PI/7,"F"_, Rule('F',"F[+F]F[-F]F"_)) <<
                   System(PI/9,"F"_, Rule('F',"F[+F]F[-F][F]"_)) <<
                   System(PI/8,"F"_, Rule('F',"FF-[-F+F+F]+[+F-F-F]"_)) <<
                   System(PI/9,"X"_, Rule('X',"F[+X]F[-X]+X"_), Rule('F',"FF"_)) <<
                   System(PI/7,"X"_, Rule('X',"F[+X][-X]FX"_), Rule('F',"FF"_)) <<
                   System(PI/8,"X"_, Rule('X',"F-[[X]+X]+F[+FX]-X"_), Rule('F',"FF"_)) <<
                   // Stochastic
                   System(PI/7,"F"_, Rule('F',"F[+F]F[-F]F"_), Rule('F',"F[+F]F"_), Rule('F',"F[-F]F"_)) <<
                   // Context sensitive
                   System(PI/2,"BAAAAAAAA"_, Rule('A',"B"_,"B"_), Rule('B',"A"_)) << //Wave propagation
                   System(PI/3,"B[+A]A[-A]A[+A]A"_,Rule('A',"B"_,"B"_,""_)) << //Acropetal signal
                   System(PI/3,"A[+A]A[-A]A[+A]B"_,Rule('A',"B"_,""_,"B"_)); //Basipetal signal

        /*debug(
        for(int unused level: range(9)) log(systems.last().generate(level));
        exit();
        )*/

        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Key(KP_Sub)).connect([this]{if(level>0) level--; window.render();});
        window.localShortcut(Key(KP_Add)).connect([this]{if(level<16) level++; window.render();});
        window.localShortcut(Key(LeftArrow)).connect([this]{if(current>0){ current--; level=0; } window.render();});
        window.localShortcut(Key(RightArrow)).connect([this]{if(current<systems.size()-1){ current++; level=0; } window.render();});
        window.localShortcut(Key(' ')).connect([this]{label=!label; window.render();});
        debug(current=systems.size()-1);
    }

    void render(int2, int2 window) override {
        this->window.setTitle(string("#"_+dec(current)+"@"_+dec(level)));
        struct State { vec2 position, heading; };
        array<State> stack;
        State state __(vec2(0,0), vec2(0,1));
        struct Line { byte label; vec2 a,b; };
        array<Line> lines;
        vec2 min=0,max=0;
        const System& system = systems[current];
        float angle = system.angle;
        for(byte command : system.generate(level)) {
            /**/  if(command=='-') { state.heading=mat2(cos(-angle),-sin(-angle),sin(-angle),cos(-angle))*state.heading; }
            else if(command=='+') { state.heading=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*state.heading; }
            else if(command=='[') { stack << state; }
            else if(command==']') { state = stack.pop(); }
            else { // all other letters are "forward" (uppercase draws a line)
                vec2 next = state.position+state.heading;
                if(command>='A' && command<='Z') lines << Line __(command,state.position,next);
                state.position = next;
                min=::min(min,state.position);
                max=::max(max,state.position);
            }
        }
        mat3 m;
        vec2 size = max-min;
        float scale = ::min(window.x/size.x,window.y/size.y);
        m.scale(scale);
        vec2 margin = vec2(window)/scale-size;
        m.translate(-min+margin/2.f);
        for(Line line: lines) {
            vec2 a=m*line.a, b=m*line.b;
            ::line(a.x,window.y-a.y,b.x,window.y-b.y);
            //::line(round(a.x),round(window.y-a.y),round(b.x),round(window.y-b.y));
            vec2 c = (a+b)/2.f;
            if(label) Text(string(str(line.label))).render(int2(round(c.x),round(window.y-c.y)));
        }
    }
} application;
