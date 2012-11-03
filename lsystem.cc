// TODO: parametric, parser, 3D, wide lines, polygons, instances, navigation
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

struct Module {
    byte symbol;
    array<float> arguments;
    explicit Module(byte symbol,ref<float> arguments=__()):symbol(symbol),arguments(arguments){}
    explicit Module(byte symbol, float a1):symbol(symbol){arguments<<a1;}
    explicit Module(byte symbol, float a1, float a2):symbol(symbol){arguments<<a1<<a2;}
    operator char() const { return symbol; }
};
string str(const Module& o) { return str(o.symbol)+"("_+str(o.arguments,',')+")"_; }
Module copy(const Module& o){return Module(o.symbol,o.arguments);}

struct Rule {
    ref<byte> left; byte edge; ref<byte> right;
    typedef function<bool(ref<float>)> Condition; Condition condition;
    typedef function<array<Module>(ref<float>)> Production; Production production;
    Rule(ref<byte> left, byte edge, ref<byte> right, Condition condition, Production production)
        : left(left),edge(edge),right(right),condition(condition),production(production){
    }
    Rule(ref<byte> left, byte edge, ref<byte> right, Condition condition, ref<byte> production)
        : Rule(left,edge,right,condition,
               [production](ref<float>)->array<Module>{array<Module> a; for(byte symbol:production) a << Module(symbol); return a; } ) {}
    Rule(ref<byte> left, byte edge, ref<byte> right, ref<byte> production):Rule(left,edge,right,[](ref<float>){return true;},production){}
    Rule(byte edge, ref<byte> production):Rule(""_,edge,""_,production){}
};

struct System {
   float angle;
   array<Module> axiom;
   array<Rule> rules;
   System(float angle, array<Module>&& axiom):angle(angle),axiom(move(axiom)){}
   template<class... Args> System(float angle, array<Module>&& axiom, Rule&& rule, Args... args)
       : System(angle,move(axiom),move(args)___) { rules<<move(rule); }
   System(float angle, ref<byte> axiom):angle(angle){for(byte symbol:axiom) this->axiom << Module(symbol);}
   template<class... Args> System(float angle, ref<byte> axiom, Rule&& rule, Args... args)
       : System(angle,axiom,move(args)___) { rules<<move(rule); }
   array<Module> generate(int level) const {
       array<Module> code = copy(axiom);
       for(int unused i: range(level)) {
           array<Module> next;
           for(uint i: range(code.size())) { const Module& c = code[i];
               array<array<Module>> matches; bool sensitive=false;
               for(const Rule& r: rules) {
                   if(r.edge==c.symbol && r.condition(c.arguments)) {
                       //TODO: parametric conditions
                       if(r.left || r.right) {
                           if(!sensitive) matches.clear(), sensitive=true;
                           if(r.left) {
                               array<byte> path;
                               for(int j=i-1; path.size()<r.left.size && j>=0; j--) {
                                   char c = code[j];
                                   if(c==']') { while(code[j].symbol!='[') j--; continue; } // skip brackets
                                   if(c=='+' || c=='-' || c=='F' || c=='[') continue;
                                   path << c;
                               }
                               if(path!=r.left) continue;
                           }
                           if(r.right) {
                               array<byte> path;
                               for(uint j=i+1; path.size()<r.right.size && j<code.size(); j++) {
                                   char c = code[j];
                                   if(c=='[') { while(code[j].symbol!=']') j++; continue; } // skip brackets
                                   if(c=='+' || c=='-' || c=='F') continue;
                                   path << c;
                               }
                               if(path!=r.right) continue;
                           }
                       } else if(sensitive) continue;
                       matches << r.production(c.arguments);
                   }
               }
               assert(matches.size()<=1);
               if(matches) next << copy(matches[random()%matches.size()]);
               else next << c;
           }
           code = move(next);
       }
       return code;
   }
};

/// Bracketed, Stochastic, context-sensitive, parametric L-system
struct LSystem : Widget {

    array<System> systems;

    Window window __(this,int2(0,0),"L-System"_);
    uint current=0, level=4; bool label=false;

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
                   System(PI/2,"BAAAAAAAA"_, Rule("B"_,'A',""_,"B"_), Rule('B',"A"_)) << //Wave propagation
                   System(PI/3,"B[+A]A[-A]A[+A]A"_,Rule("B"_,'A',""_,"B"_)) << //Acropetal signal
                   System(PI/3,"A[+A]A[-A]A[+A]B"_,Rule(""_,'A',"B"_,"B"_)) << //Basipetal signal
                   //Hogeweg and Hesper plant
                   System(PI/8,"F1F1F1"_,
                          Rule("0"_,'0',"0"_,"0"_),
                          Rule("0"_,'0',"1"_,"1[+F1F1]"_),
                          Rule("0"_,'1',"0"_,"1"_),
                          Rule("0"_,'1',"1"_,"1"_),
                          Rule("1"_,'0',"0"_,"0"_),
                          Rule("1"_,'0',"1"_,"1F1"_),
                          Rule("1"_,'1',"0"_,"0"_),
                          Rule("1"_,'1',"1"_,"0"_),
                          Rule(""_,'+',""_,"-"_),
                          Rule(""_,'-',""_,"+"_)) <<
                   // Parametric
                   ({array<Module> axiom; axiom << Module('B',2) << Module('A',4,4);
                     System(0,move(axiom),
                     Rule(""_,'A',""_, [](ref<float> a){return a[1]<=3;},
                     [](ref<float> a)->array<Module>{array<Module> p; p << Module('A',a[0]*2,a[0]+a[1]); return p;}),
                     Rule(""_,'A',""_, [](ref<float> a){return a[1]>3;},
                     [](ref<float> a)->array<Module>{array<Module> p; p << Module('B',a[0]) << Module('A',a[0]/a[1],0); return p;}),
                     Rule(""_,'B',""_, [](ref<float> a){return a[0]<1;}, "C"_),
                     Rule(""_,'B',""_, [](ref<float> a){return a[0]>=1;},
                     [](ref<float> a)->array<Module>{array<Module> p; p << Module('B',a[0]-1); return p;})
                     ); });
        debug(
        for(int unused level: range(5)) log(systems.last().generate(level));
        exit();
        )

        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Key(KP_Sub)).connect([this]{if(level>0) level--; window.render();});
        window.localShortcut(Key(KP_Add)).connect([this]{if(level<32) level++; window.render();});
        window.localShortcut(Key(LeftArrow)).connect([this]{if(current>0){ current--; if(level>4) level=4; } window.render();});
        window.localShortcut(Key(RightArrow)).connect([this]{if(current<systems.size()-1){ current++; if(level>4) level=4; } window.render();});
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
        for(const Module& module : system.generate(level)) { char symbol=module.symbol;
            /**/  if(symbol=='-') { state.heading=mat2(cos(-angle),-sin(-angle),sin(-angle),cos(-angle))*state.heading; }
            else if(symbol=='+') { state.heading=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*state.heading; }
            else if(symbol=='[') { stack << state; }
            else if(symbol==']') { state = stack.pop(); }
            else if(symbol>='A' && symbol<='Z') { // uppercase letters move forwards drawing a line
                vec2 next = state.position+state.heading;
                lines << Line __(module.symbol,state.position,next);
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
            vec2 c = (a+b)/2.f;
            if(label) Text(string(str(line.label))).render(int2(round(c.x),round(window.y-c.y)));
        }
    }
} application;
