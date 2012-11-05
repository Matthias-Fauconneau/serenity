// TODO: polygons (wide lines), parser, herbaceous plants
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
    explicit Module(byte symbol, float a1, float a2, float a3):symbol(symbol){arguments<<a1<<a2<<a3;}
    operator byte() const { return symbol; }
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
string str(const Rule& o) { return str(o.left)+"<"_+str(o.edge)+">"_+str(o.right); }

struct System {
   float angle;
   ref<byte> ignore;
   array<Module> axiom;
   array<Rule> rules;
   System(float angle, ref<byte> ignore, array<Module>&& axiom):angle(angle),ignore(ignore),axiom(move(axiom)){}
   template<class... Args> System(float angle, ref<byte> ignore, array<Module>&& axiom, Rule&& rule, Args... args)
       : System(angle,ignore,move(axiom),move(args)___) { rules<<move(rule); }
   System(float angle, ref<byte> ignore, ref<byte> axiom):angle(angle),ignore(ignore){
       for(byte symbol:axiom) this->axiom << Module(symbol);}
   template<class... Args> System(float angle, ref<byte> ignore, ref<byte> axiom, Rule&& rule, Args... args)
       : System(angle,ignore,axiom,move(args)___) { rules<<move(rule); }
   array<Module> generate(int level) const {
       array<Module> code = copy(axiom);
       for(int unused i: range(level)) {
           array<Module> next;
           for(uint i: range(code.size())) { const Module& c = code[i];
               array<array<Module>> matches; //bool sensitive=false;
               for(const Rule& r: rules) {
                   if(r.edge!=c.symbol) continue;
                   if(r.left || r.right) {
                       array<float> arguments;
                       if(r.left) {
                           array<byte> path;
                           for(int j=i-1; path.size()<r.left.size && j>=0; j--) {
                               const Module& c = code[j];
                               if(c==']') { while(code[j]!='[') j--; continue; } // skip brackets
                               if(c=='+' || c=='-' || c=='[' || ignore.contains(c)) continue;
                               path << c;
                               arguments << c.arguments;
                           }
                           if(path!=r.left) continue;
                       }
                       arguments << c.arguments;
                       if(r.right) {
                           array<byte> path;
                           for(uint j=i+1; path.size()<r.right.size && j<code.size(); j++) {
                               const Module& c = code[j];
                               if(c=='[') { while(code[j]!=']') j++; continue; } // skip brackets
                               if(c=='+' || c=='-' || ignore.contains(c)) continue;
                               path << c;
                               arguments << c.arguments;
                           }
                           if(path!=r.right) continue;
                       }
                       if(!r.condition(arguments)) continue;
                       //if(!sensitive) matches.clear(), sensitive=true;
                       matches << r.production(arguments);
                   } else {
                       //if(sensitive) continue;
                       if(!r.condition(c.arguments)) continue;
                       matches << r.production(c.arguments);
                   }
               }
               //assert(matches.size()<=1);
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
    uint current=0, level=6; bool label=false;
    float yaw=0,pitch=0;

    LSystem() {
        systems <<
                // Abstract curves
                   System(PI/2,""_, "-F"_, Rule('F',"F+F-F-F+F"_)) <<
                   System(PI/2,""_, "F-F-F-F"_, Rule('F',"F-F+F+FF-F-F+F"_)) <<
                   System(PI/2,""_, "F-F-F-F"_, Rule('F',"FF-F-F-F-F-F+F"_)) <<
                   System(PI/2,""_, "F-F-F-F"_, Rule('F',"FF-F-F-F-FF"_)) <<
                   System(PI/2,""_, "F-F-F-F"_, Rule('F',"FF-F--F-F"_)) <<
                   System(PI/3,""_, "F--F--F"_, Rule('F',"F+F--F+F"_)) <<
                   /*System(PI/3,""_, "R"_, Rule('L',"R+L+R"_), Rule('R',"L-R-L"_)) << // Sierpinski gasket
                   System(PI/2,""_, "L"_, Rule('L',"L+R+"_), Rule('R',"-L-R"_)) << // Dragon curve
                   System(PI/3,""_, "L"_, Rule('L',"L+R++R-L--LL-R+"_), Rule('R',"-L+RR++R+L--L-R"_)) << // Hexagonal Gosper*/
                   // Plants
                   System(PI/7,""_, "F"_, Rule('F',"F[+F]F[-F]F"_)) <<
                   System(PI/9,""_, "F"_, Rule('F',"F[+F]F[-F][F]"_)) <<
                   System(PI/8,""_, "F"_, Rule('F',"FF-[-F+F+F]+[+F-F-F]"_)) <<
                   System(PI/9,""_, "X"_, Rule('X',"F[+X]F[-X]+X"_), Rule('F',"FF"_)) <<
                   System(PI/7,""_, "X"_, Rule('X',"F[+X][-X]FX"_), Rule('F',"FF"_)) <<
                   System(PI/8,""_, "X"_, Rule('X',"F-[[X]+X]+F[+FX]-X"_), Rule('F',"FF"_)) <<
                   // 3D Plant
                   System(PI/8,""_, "A"_, Rule('A',"[&FL!A]/////'[&FL!A]///////'[&FL!A]"_), Rule('F',"S/////F"_), Rule('S',"FL"_),
                          Rule('L',"['''^^{-f+f+f-|-f+f+f}]"_)) <<
                   // Stochastic
                   System(PI/7,""_, "F"_, Rule('F',"F[+F]F[-F]F"_), Rule('F',"F[+F]F"_), Rule('F',"F[-F]F"_)) <<
                   // Context sensitive
                   System(PI/2,""_, "BAAAAAAAA"_, Rule("B"_,'A',""_,"B"_), Rule('B',"A"_)) << //Wave propagation
                   System(PI/3,""_, "B[+A]A[-A]A[+A]A"_,Rule("B"_,'A',""_,"B"_)) << //Acropetal signal
                   System(PI/3,""_, "A[+A]A[-A]A[+A]B"_,Rule(""_,'A',"B"_,"B"_)) << //Basipetal signal
                   //Hogeweg and Hesper plant
                   System(PI/8,""_, "F1F1F1"_,
                          Rule("0"_,'0',"0"_,"0"_),
                          Rule("0"_,'0',"1"_,"1[+F1F1]"_),
                          Rule("0"_,'1',"0"_,"1"_),
                          Rule("0"_,'1',"1"_,"1"_),
                          Rule("1"_,'0',"0"_,"0"_),
                          Rule("1"_,'0',"1"_,"1F1"_),
                          Rule("1"_,'1',"0"_,"0"_),
                          Rule("1"_,'1',"1"_,"0"_),
                          Rule(""_,'+',""_,"-"_),
                          Rule(""_,'-',""_,"+"_))
                   // Parametric
                << ({
                        array<Module> axiom; axiom << Module('B',2) << Module('A',4,4);
                        System(0,""_, move(axiom),
                        Rule(""_,'A',""_, [](ref<float> a){return a[1]<=3;},
                        [](ref<float> a)->array<Module>{array<Module> p; p
                                                        << Module('A',a[0]*2,a[0]+a[1]);
                                                        return p;}),
                        Rule(""_,'A',""_, [](ref<float> a){return a[1]>3;},
                        [](ref<float> a)->array<Module>{array<Module> p; p
                                                        << Module('B',a[0])
                                                        << Module('A',a[0]/a[1],0);
                                                        return p;}),
                        Rule(""_,'B',""_, [](ref<float> a){return a[0]<1;}, "C"_),
                        Rule(""_,'B',""_, [](ref<float> a){return a[0]>=1;},
                        [](ref<float> a)->array<Module>{array<Module> p; p
                                                        << Module('B',a[0]-1);
                                                        return p;})
                        ); })
                   // Anabaena with heterocysts
                << ({
                        const float CH = 900 /*high concentration*/, CT=0.4 /*concentration threshold */, ST=3.9 /*segment size threshold*/;
                        array<Module> axiom; axiom << Module('-',PI/2) << Module('F',0,0,CH) << Module('F',4,1,CH) << Module('F',0,0,CH);
                        System(0,"f~H"_,move(axiom),
                        //F(s,t,c) : t=1 & s>=6 → F(s/3*2,2,c)f(1)F(s/3,1,c)
                        Rule(""_,'F',""_, [](ref<float> a){return a[1]==1 && a[0]>=6;},
                        [](ref<float> a)->array<Module>{array<Module> p; p
                                                        << Module('F',a[0]/3*2,2,a[2])
                                                        << Module('f',1)
                                                        << Module('F',a[0]/3,1,a[2]);
                                                        return p;}),
                        //F(s,t,c) : t=2 & s>=6 → F(s/3,2,c)f(1)F(s/3*2,1,c)
                        Rule(""_,'F',""_, [](ref<float> a){return a[1]==1 && a[0]>=6;},
                        [](ref<float> a)->array<Module>{array<Module> p; p
                                                        << Module('F',a[0]/3,2,a[2])
                                                        << Module('f',1)
                                                        << Module('F',a[0]/3*2,1,a[2]);
                                                        return p;}),
                        //F(h,i,k) < F(s,t,c) > F(o,p,r) : s>ST|c>CT → F(s+.1,t,c+0.25*(k+r-3*c))
                        Rule("F"_,'F',"F"_, [ST,CT](ref<float> a){return a[3+0]>ST || a[3+2]>CT;},
                        [](ref<float> a)->array<Module>{array<Module> p; p
                                                        << Module('F',a[3+0]+0.1,a[3+1],a[2]+(a[2]-3*a[3+2]+a[2*3+2])/4);
                                                        return p;}),
                        //F(h,i,k) < F(s,t,c) > F(o,p,r) : !(s>ST|c>CT) → F(0,0,CH) ∼ H(1)
                        Rule("F"_,'F',"F"_, [ST,CT](ref<float> a){return !(a[3+0]>ST || a[3+2]>CT);},
                        [CH](ref<float>)->array<Module>{array<Module> p; p
                                                        << Module('F',0,0,CH)
                                                        << Module('~')
                                                        << Module('H',1);
                                                        return p;}),
                        //H(s) : s<3 → H(s*1.1)
                        Rule(""_,'H',""_, [](ref<float> a){return a[0]<3;},
                        [](ref<float> a)->array<Module>{array<Module> p; p
                                                        << Module('H',a[0]*1.1);
                                                        return p;})
                        ); })
                   // Row of trees
                << ({
                        const float c=1, p=0.3, q=c-p, h=sqrt(p*q);
                        array<Module> axiom; axiom << Module('F',1,0);
                        System(86*PI/180,""_,move(axiom),
                        //F(x,t) : t=0 → F(x∗p,2) + F(x∗h,1) - - F(x∗h,1) + F(x∗q,0)
                        Rule(""_,'F',""_,[](ref<float> a){return a[1]==0;},
                        [p,q,h](ref<float> a)->array<Module>{array<Module> r; r
                                                             << Module('F',a[0]*p,2)
                                                             << Module('+')
                                                             << Module('F',a[0]*h,1)
                                                             << Module('-') << Module('-')
                                                             << Module('F',a[0]*h,1)
                                                             << Module('+')
                                                             << Module('F',a[0]*q,0);
                                                             return r;}),
                        //F(x,t) : t>0 → F(x, t-1)
                        Rule(""_,'F',""_,[](ref<float> a){return a[1]>0;},
                        [](ref<float> a)->array<Module>{array<Module> r; r
                                                        << Module('F',a[0],a[1]-1);
                                                        return r;})
                        ); })
                   // Growing branching pattern
                << ({
                        const float R=1.456;
                        array<Module> axiom; axiom << Module('A');
                        System(85*PI/180,""_,move(axiom),
                        //A → F(1)[+A][−A]
                        Rule(""_,'A',""_,[](ref<float>){return true;},
                        [](ref<float>)->array<Module>{array<Module> r; r
                                                         << Module('F',1)
                                                         << Module('[') << Module('+') << Module('A') << Module(']')
                                                         << Module('[') << Module('-') << Module('A') << Module(']');
                                                         return r;}),
                        //F(s) → F(s∗R)
                        Rule(""_,'F',""_,[](ref<float>){return true;},
                        [R](ref<float> a)->array<Module>{array<Module> r; r
                                                         << Module('F',a[0]*R);
                                                         return r;})
                        ); })
                   // Honda monopodial tree-like structure
                << ({
                        const float r1=0.9/*trunk contraction ratio*/, r2=0.6/*branches contraction ratio*/, a0=PI/4/*trunk branching angle*/,
                        a2=PI/4/*lateral axes branching angles*/,d=137.5*PI/180/*divergence angle*/,wr=1/sqrt(2)/*width decrease rate*/;
                        array<Module> axiom; axiom << Module('A',1,10);
                        System(0,""_,move(axiom),
                        //main axis: A(l,w) → !(w)F(l)[&(a0)B(l*r2 ,w*wr )]/(d)A(l*r1,w*wr )
                        Rule(""_,'A',""_,[](ref<float>){return true;},
                        [a0,r1,r2,wr,d](ref<float> a)->array<Module>{array<Module> r; r
                                                                   << Module('!',a[1])
                                                                   << Module('F',a[0])
                                                                   << Module('[') << Module('&',a0) << Module('B',a[0]*r2,a[1]*wr) << Module(']')
                                                                   << Module('/',d) << Module('A',a[0]*r1,a[1]*wr);
                                                                   return r;}),
                        //lateral branch: B(l,w) → !(w)F(l)[-(a2)$C(l*r2 ,w*wr)]C(l*r1 ,w*wr)
                        Rule(""_,'B',""_,[](ref<float>){return true;},
                        [a2,r1,r2,wr](ref<float> a)->array<Module>{array<Module> r; r
                                                                   << Module('!',a[1])
                                                                   << Module('F',a[0])
                                                                   << Module('[') << Module('-',a2) << Module('$') << Module('C',a[0]*r2,a[1]*wr) << Module(']')
                                                                   << Module('C',a[0]*r1,a[1]*wr);
                                                                   return r;}),
                        //alternate lateral branch: C(l,w) → !(w)F(l)[+(a2)$B(l*r2 ,w*wr)]B(l*r1 ,w*wr)
                        Rule(""_,'C',""_,[](ref<float>){return true;},
                        [a2,r1,r2,wr](ref<float> a)->array<Module>{array<Module> r; r
                                                                   << Module('!',a[1])
                                                                   << Module('F',a[0])
                                                                   << Module('[') << Module('+',a2) << Module('$') << Module('B',a[0]*r2,a[1]*wr) << Module(']')
                                                                   << Module('B',a[0]*r1,a[1]*wr);
                                                                   return r;})
                        ); })
                   // Aono-Kunii sympodial tree-like structure
                << ({
                        const float r1=0.9, r2=0.7/*contraction ratios*/, a1=10*PI/180,a2=60*PI/180/*branching angles*/,wr=1/sqrt(2)/*width decrease rate*/;
                        array<Module> axiom; axiom << Module('A',1,10);
                        System(0,""_,move(axiom),
                        //trunk: A(l,w) → !(w)F(l)[&(a1)B(l*r1,w*wr )]/(180)[&(a2)B(l*r2,w*wr)]
                        Rule(""_,'A',""_,[](ref<float>){return true;},
                        [a1,a2,r1,r2,wr](ref<float> a)->array<Module>{array<Module> r; r
                                                                      << Module('!',a[1])
                                                                      << Module('F',a[0])
                                                                      << Module('[') << Module('&',a1) << Module('B',a[0]*r1,a[1]*wr) << Module(']')
                                                                      << Module('/',PI)
                                                                      << Module('[') << Module('&',a2) << Module('B',a[0]*r2,a[1]*wr) << Module(']');
                                                                      return r;}),
                        //branch: B(l,w) → !(w)F(l)[+(a1)$B(l*r1,w*wr )][-(a2)$B(l*r2,w*wr )]
                        Rule(""_,'B',""_,[](ref<float>){return true;},
                        [a1,a2,r1,r2,wr](ref<float> a)->array<Module>{array<Module> r; r
                                                                   << Module('!',a[1])
                                                                   << Module('F',a[0])
                                                                   << Module('[') << Module('+',a1) << Module('$') << Module('B',a[0]*r1,a[1]*wr) << Module(']')
                                                                   << Module('[') << Module('-',a2) << Module('$') << Module('B',a[0]*r2,a[1]*wr) << Module(']');
                                                                   return r;})
                        ); })
                   // Ternary tree-like structure
                << ({
                        const float d1=94.74*PI/180, d2=132.63*PI/180/*divergence angles*/, a=18.95*PI/180/*branching angle*/,
                        lr=1.109/*elongation rate*/, wr=sqrt(3)/*width increase rate*/;
                        array<Module> axiom; axiom << Module('!',1) << Module('F',200) << Module('/',PI/4) << Module('A');
                        System(0,""_,move(axiom),
                        //ternary branch: A → !(vr)F(50)[&(a)F(50)A]/(d1)[&(a)F(50)A]/(d2 )[&(a)F(50)A]
                        Rule(""_,'A',""_,[](ref<float>){return true;},
                        [wr,a,d1,d2](ref<float>)->array<Module>{array<Module> r; r
                                                          << Module('!',wr)
                                                          << Module('F',50)
                                                          << Module('[') << Module('&',a) << Module('F',50) << Module('A') << Module(']')
                                                          << Module('/',d1)
                                                          << Module('[') << Module('&',a) << Module('F',50) << Module('A') << Module(']')
                                                          << Module('/',d2)
                                                          << Module('[') << Module('&',a) << Module('F',50) << Module('A') << Module(']');
                                                          return r;}),
                        //growth: F(l) → F(l*lr)
                        Rule(""_,'F',""_,[](ref<float>){return true;},
                        [lr](ref<float> a)->array<Module>{array<Module> r; r
                                                          << Module('F',a[0]*lr);
                                                          return r;}),
                        //growth: !(w) → !(w*wr)
                        Rule(""_,'!',""_,[](ref<float>){return true;},
                        [wr](ref<float> a)->array<Module>{array<Module> r; r
                                                          << Module('!',a[0]*wr);
                                                          return r;})
                        ); });
        //debug(for(int unused level: range(4)) log(systems.last().generate(level)); exit();)

        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Key(KP_Sub)).connect([this]{if(level>0) level--; window.render();});
        window.localShortcut(Key(KP_Add)).connect([this]{if(level<256) level++; window.render();});
        window.localShortcut(Key(KP_Divide)).connect([this]{if(current>0){current--; if(level>10) level=10;} window.render();});
        window.localShortcut(Key(KP_Multiply)).connect([this]{if(current<systems.size()-1){current++; if(level>10) level=10;} window.render();});
        window.localShortcut(Key(' ')).connect([this]{label=!label; window.render();});
        //TODO: mouse navigation
        window.localShortcut(LeftArrow).connect([this]{yaw-=PI/12; window.render();});
        window.localShortcut(RightArrow).connect([this]{yaw+=PI/12; window.render();});
        window.localShortcut(DownArrow).connect([this]{pitch-=PI/12; window.render();});
        window.localShortcut(UpArrow).connect([this]{pitch+=PI/12; window.render();});
        debug(current=systems.size()-1);
    }

    void render(int2, int2 window) override {
        this->window.setTitle(string("#"_+dec(current)+"@"_+dec(level)));
        // Turtle interpretation of modules string generated by an L-system
        array<mat4> stack; array<float> lineWidthStack;
        mat4 state; float lineWidth=1;
        struct Line { byte label; vec3 a,b; float w; };
        array<Line> lines;
        vec3 min=0,max=0;
        const System& system = systems[current];
        float angle = system.angle;
        for(const Module& module : system.generate(level)) { char symbol=module.symbol;
            float a = module.arguments?module.arguments[0]:angle;
            if(symbol=='\\'||symbol=='/') state.rotateX(symbol=='\\'?a:-a);
            else if(symbol=='&'||symbol=='^') state.rotateY(symbol=='&'?a:-a);
            else if(symbol=='-' ||symbol=='+') state.rotateZ(symbol=='+'?a:-a);
            else if(symbol=='!') lineWidth=a;
            else if(symbol=='$') { //set Y horizontal (keeping X), Z=X×Y
                vec3 X; for(int i=0;i<3;i++) X[i]=state(i,0);
                vec3 Y = cross(vec3(1,0,0),X);
                float y = length(Y);
                if(y<0.01) continue; //X is colinear to vertical (all possible Y are already horizontal)
                Y /= y;
                assert(Y.x==0);
                vec3 Z = cross(X,Y);
                for(int i=0;i<3;i++) state(i,1)=Y[i], state(i,2)=Z[i];
            }
            else if(symbol=='[') stack << state, lineWidthStack << lineWidth;
            else if(symbol==']') state = stack.pop(), lineWidth = lineWidthStack.pop();
            else if(symbol=='f' || symbol=='F') {
                float step = module.arguments?module.arguments[0]:1;
                vec3 a = (state*vec3(0,0,0));
                state.translate(vec3(step,0,0)); //forward axis is +X
                vec3 b = (state*vec3(0,0,0));
                if(symbol=='F') lines << Line __(module.symbol,a,b,lineWidth);
                min=::min(min,b);
                max=::max(max,b);
                // Apply tropism
                vec3 X; for(int i=0;i<3;i++) X[i]=state(i,0);
                vec3 Y = cross(vec3(-1,0,0),X);
                float y = length(Y);
                if(y<0.01) continue; //X is colinear to tropism (all rotations are possible)
                assert(Y.x==0);
                state.rotate(0.22,state.inverse().normalMatrix()*Y);
            }
        }

        // Fit window
        mat3 fit; {
            mat4 view;
            view.rotateZ(PI/2); //+X (heading) is up
            if(1) { //Fit window (for normalized fractal curves)
                vec2 m=::min(view*min,view*max).xy(), M=::max(view*min,view*max).xy();
                vec2 size = M-m;
                float scale = ::min(window.x/size.x,window.y/size.y)*0.5;
                fit.scale(scale);
                vec2 margin = vec2(window)/scale-size;
                fit.translate(vec2(vec2(vec2(window)/scale/2.f).x,vec2(-m+margin/2.f).y));
            } else { //Fixed size (for growing trees)
                fit.translate(vec2(window.x/2,0)); fit.scale(16);
            }
        }

        // Render lines (TODO: line width)
        {
            mat4 view;
            view.rotateX(pitch); // pitch
            view.rotateY(yaw); // yaw
            view.rotateZ(PI/2); //+X (heading) is up
            for(Line line: lines) {
                vec2 a=fit*(view*line.a).xy(), b=fit*(view*line.b).xy();
                ::line(a.x,window.y-a.y,b.x,window.y-b.y);
                vec2 c = (a+b)/2.f;
                if(label) Text(string(str(line.label))).render(int2(round(c.x),round(window.y-c.y)));
            }
        }
    }
} application;
