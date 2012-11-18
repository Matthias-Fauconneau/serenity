/** \file lsystem.cc
This is a demonstration of software rasterization as presented by Michael Abrash in "Rasterization on Larrabee".

First, an L-System generates our "scene", i.e a list of branches (position+diameter at both end).
Then, for each frame, the branches are transformed to view-aligned quads (2 triangles per branch) and submitted to the rasterizer.

This rasterizer is an AVX implementation of a tile-based deferred renderer (cf http://www.drdobbs.com/parallel/rasterization-on-larrabee/217200602) :
Triangles are not immediatly rendered but first sorted in 16x16 pixels (64x64 samples) bins.
When all triangles have been setup, each tile is separately rendered.
Tiles can be processed in parallel (I'm using 4 hyperthreaded cores) and only access their local framebuffer (which should fit L1).
As presented in Abrash's article, rasterization is done recursively (4x4 blocks of 4x4 pixels of 4x4 samples) using precomputed step grids.
This architecture allows the rasterizer to leverage 16-wide vector units (on Ivy Bride's 8-wide units, all operations have to be duplicated).
For each face, the rasterizer outputs pixel masks for each blocks (or sample masks for partial pixels).
Then, pixels are depth-tested, shaded and blended in the local framebuffer.
Finally, after all passes have been rendered, the tiles are resolved and copied to the application window buffer.

My goal is to minimize any sampling artifacts:
- The tiled renderer allows to use 16xMSAA without killing performance (since it is not bandwidth-limited)
- The branches (actually cones) are not tesselated. They are rendered as view-aligned quads and shaded accordingly.
- Shadows are raycasted (and not sampled from a shadow map).

For this simple scene, a shadow map would be a better choice, but I wanted to try to leverage the CPU in the shader.

The main advantage of raycasting is that it allows to implement accurate contact-hardening soft shadows for scenes with arbitrary depth complexity.
Shadow maps only allows to sample the furthest (nearest to light) occluder, while raycasting correctly handle penumbra affected by a closer hidden occluder.
Thus, given enough samples, raycasting can simulate much wider lights.

The main issue of raycasting is that performance is already so awful while 16 samples is not enough to avoid noisy shadows.
I guess there is much room for optimization, but actually I originally wanted to study L-Systems (I'm not through "The Algorithmic Beauty of Plants" yet).

I worked on this project to have a more flexible real-time renderer for CG experiments.
But also because working on improving performance is addictive. As in a game, you get a score (frame time) and you have to make the most out of your abilities to improve the score.

My current results (8 threads on 4-core Ivy Bridge):
- without shadows: 43-46 fps
- with 16x raycasted shadows: 13-16 fps

You can find the source code on github.
Rasterizer code: https://github.com/Matthias-Fauconneau/serenity/blob/master/raster.h
Setup and shader: https://github.com/Matthias-Fauconneau/serenity/blob/master/lsystem.cc
This is mostly for reference, since this application use my own framework (Linux only).
*/
#include "data.h"
#include "matrix.h"
#include "process.h"
#include "raster.h"
#include "window.h"
#include "interface.h"
#include "text.h"
#include "time.h"
#include "png.h"

// Light

inline float acos(float t) { return __builtin_acosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }
/// Directional light with angular diameter
inline float angularLight(vec3 surfaceNormal, vec3 lightDirection, float angularDiameter) {
    float t = ::acos(dot(lightDirection,surfaceNormal)); // angle between surface normal and light principal direction
    float a = min<float>(PI/2,max<float>(-PI/2,t-angularDiameter/2)); // lower bound of the light integral
    float b = min<float>(PI/2,max<float>(-PI/2,t+angularDiameter/2)); // upper bound of the light integral
    float R = sin(b) - sin(a); // evaluate integral on [a,b] of cos(t-dt)dt (lambert reflectance model) //TODO: Oren-Nayar
    R /= 2*sin(angularDiameter/2); // normalize
    return R;
}
/// For an hemispheric light, the integral bounds are always [t-PI/2,PI/2], thus R evaluates to (1-cos(t))/2
inline float hemisphericLight(vec3 surfaceNormal, vec3 lightDirection) { return (1+dot(lightDirection,surfaceNormal))/2; }
/// This is the limit of angularLight when angularDiameter → 0
inline float directionnalLight(vec3 surfaceNormal, vec3 lightDirection) { return max(0.f,dot(lightDirection,surfaceNormal)); }

// Shadow

struct Face {
    mat3 E; //edge functions
    vec3 iw, iz;
};
/// Shadow sampling patterns
static struct Pattern {
    vec16 X,Y;
    Pattern() {
        // jittered radial sample pattern
        Random random;
        for(int u=0;u<4;u++) for(int v=0;v<4;v++) {
            float r = sqrt( (u + (random+1)/2.f) / 4.f );
            float t = (v + (random+1)/2.f) / 4.f;
            X[u*4+v] = r*cos(2*PI*t);
            Y[u*4+v] = r*sin(2*PI*t);
        }
    }
} patterns[16];
Random randomPattern;

/// Stores shadow occluders for shadow-receiving surfaces
struct Shadow {
    // Occluders geometry (in light space)
    Face* faces;
    uint faceCapacity;
    uint faceCount=0;

    // 2D space partitioning to accelerate shadow ray casts
    struct Bin { uint16 faceCount=0; uint16 faces[2047]; };
    int width, height;
    Bin* bins;

    Shadow(vec2 lightSpaceSize, uint faceCapacity):faceCapacity(faceCapacity){
        width = ceil(lightSpaceSize.x);
        height = ceil(lightSpaceSize.y);
        bins = allocate64<Bin>(width*height);
        faces = allocate64<Face>(faceCapacity);
        for(uint i: range(width*height)) bins[i].faceCount=0;
        randomPattern.reset(); //keep same pattern at each frame
    }
    ~Shadow() { unallocate(bins,width*height); unallocate(faces,faceCapacity); }

    void submit(vec4 A, vec4 B, vec4 C) {
        assert(abs(A.w-1)<0.01); assert(abs(B.w-1)<0.01); assert(abs(C.w-1)<0.01);
        if(faceCount>=faceCapacity) error("Face overflow");

        Face& face = faces[faceCount];
        mat3& E = face.E;
        E = mat3(A.xyw(), B.xyw(), C.xyw());
        float det = E.det(); if(det<0.001f) return; //small or back-facing triangle
        E = E.cofactor(); //edge equations are now columns of E
        face.iw = E[0]+E[1]+E[2];
        face.iz = E*vec3(A.z,B.z,C.z);

        int2 min = ::max(int2(0,0),int2(floor(::min(::min(A.xy(),B.xy()),C.xy()))));
        int2 max = ::min(int2(width-1,height-1),int2(ceil(::max(::max(A.xy(),B.xy()),C.xy()))));
        //FIXME: This is not conservative enough for large sample patterns (far occluder)
        for(int binY=min.y; binY<=max.y; binY++) for(int binX=min.x; binX<=max.x; binX++) {
            Bin& bin = bins[binY*width+binX];
            if(bin.faceCount>=sizeof(bin.faces)/sizeof(uint16)) error("Index overflow",binX,binY,bin.faceCount);
            bin.faces[bin.faceCount++]=faceCount;
        }
        faceCount++;
    }

    inline float operator()(vec3 lightPos) const {
        const Pattern& pattern = patterns[randomPattern()%16];
        vec16 samples = 0;
        uint x=lightPos.x, y=lightPos.y;
        if(x<(uint)width && y<(uint)height) {
            const Bin& bin = bins[y*width+x];
            for(uint i: range(bin.faceCount)) {
                uint index = bin.faces[i];
                const Face& face = faces[index];
                const vec3 XY1(lightPos.x,lightPos.y,1.f);
                float w = 1/dot(face.iw,XY1);
                float z = w*dot(face.iz,XY1);
                float d = lightPos.z-z;
                if(d<=1.f) continue; //only Z-test midpoint

                // scale sample pattern to soften shadow depending on occluder distance
                const float A0=1/16.f, dzA=1/256.f; //FIXME: a should be pixel area in light space, b ~ tan(light angular diameter)
                vec16 lightX = lightPos.x + (A0+dzA*d)*pattern.X, lightY = lightPos.y + (A0+dzA*d)*pattern.Y;

                vec16 a = face.E[0].x * lightX + face.E[0].y * lightY + face.E[0].z;
                vec16 b = face.E[1].x * lightX + face.E[1].y * lightY + face.E[1].z;
                vec16 c = face.E[2].x * lightX + face.E[2].y * lightY + face.E[2].z;
                samples = samples | ((a > 0.f) & (b > 0.f) & (c > 0.f));
            }
        }
        return __builtin_popcount(mask(samples))/16.f;
    }
};

// Shaders

static constexpr vec3 skyColor = vec3(1./2,1./4,1./8);
static constexpr vec3 sunColor = vec3(1./2,3./4,7./8);

/// Base shader for shadow-receiving surfaces (e.g ground)
struct ShadowShader {
    // Uniform attributes
    const Shadow& shadow;

    // Shader specification (used by rasterizer)
    struct FaceAttributes {};
    static constexpr int V = 3; // number of interpolated vertex attributes (3D light space position)
    static constexpr bool blend=false; // Disable unecessary blending

    ShadowShader(const Shadow& shadow):shadow(shadow){}

    // Default shader is a white shadow-receiving surface
    vec4 operator()(FaceAttributes, float varying[V]) const {
        float sunLight = 1.f-shadow(vec3(varying[0],varying[1],varying[2]));
        return vec4(skyColor+sunLight*sunColor,1.f);
    }
};

/// Shader for flat surfaces (e.g leaves) lit by hemispheric sky and shadow-casting sun lights.
struct FlatShader : ShadowShader {
    // Uniform attributes
    vec3 sky, sun; bool enableShadow;

    // Shader specification (used by rasterizer)
    struct FaceAttributes {
        vec3 N; // Surface normal for flat shading
        vec3 albedo; // Surface color
    };

    FlatShader(const Shadow& shadow):ShadowShader(shadow){} //FIXME: inherit constructor

    vec4 operator()(FaceAttributes face, float varying[V]) const {
        float sunLight=1.f;
        if(enableShadow) {
            vec3 lightPos = vec3(varying[0],varying[1],varying[2]);
            sunLight -= shadow(lightPos);
        }

        // Computes diffused light from hemispheric sky light and shadow-casting directionnal sun light
        vec3 diffuseLight
                = hemisphericLight(face.N,sky)*skyColor
                + sunLight*directionnalLight(face.N,sun)*sunColor;

        vec3 diffuse = face.albedo*diffuseLight;
        return vec4(diffuse,1.f);
    }
};

/// Shader for view-aligned cone impostors (e.g stalk, branches) lit by hemispheric sky and shadow-casting sun lights.
struct ConeShader : ShadowShader {
    // Uniform attributes
    vec3 sky, sun; bool enableShadow;

    // Shader specification (used by rasterizer)
    struct FaceAttributes {
        vec3 Y,Z; // Normal basis to fix cone lighting
        vec3 N; // Light space normal to fix position for shadow reception
        vec3 albedo; // Surface color
    };
    static constexpr int V = 4; // number of interpolated vertex attributes (cone position + 3D light space position)

    ConeShader(const Shadow& shadow):ShadowShader(shadow){} //FIXME: inherit constructor

    vec4 operator()(FaceAttributes face, float varying[V]) const {
        // Clip to avoid negative square root
        float d = clip(-1.f,varying[0],1.f);

        float sunLight=1.f;
        if(enableShadow) {
            vec3 lightPos = vec3(varying[1],varying[2],varying[3]);
            lightPos += sqrt(1-d*d)*face.N; // fix light position
            sunLight -= shadow(lightPos);
        }

        // Reconstruct cone normal
        vec3 N = d*face.Y + sqrt(1-d*d)*face.Z;

        // Computes diffused light from hemispheric sky light and shadow-casting directionnal sun light
        vec3 diffuseLight
                = hemisphericLight(N,sky)*skyColor
                + sunLight*directionnalLight(N,sun)*sunColor;

        vec3 diffuse = face.albedo*diffuseLight;
        return vec4(diffuse,1.f);
    }
};

// L-System

/// word is an index in a string table allowing fast move/copy/compare
static array<string> pool;
struct word {
    int id;
    word(const string& s) { id=pool.indexOf(s); if(id<0) { id=pool.size(); pool<<copy(s); } }
    word(const ref<byte>& s):word(string(s)){}
    explicit operator bool() const { return pool[id].size(); }
};
inline bool operator ==(word a, word b) { return a.id == b.id; }
inline const string& str(const word& w) { return pool[w.id]; }

/// An Element generated by a Production
struct Module  {
    word symbol;
    array<float> arguments;
    Module(const word& symbol, array<float>&& arguments=array<float>()):symbol(symbol),arguments(move(arguments)){}
    operator word() const { return symbol; }
};
string str(const Module& o) { return str(o.symbol)+"("_+str(o.arguments,',')+")"_; }
Module copy(const Module& o){return Module(o.symbol,copy(o.arguments));}

/// Abstract base class to represent expressions
struct Expression {
    virtual float evaluate(ref<float>) const = 0;
    virtual string str() const = 0;
};
string str(const Expression& o) { return o.str(); }
struct Immediate : Expression {
    float value;
    Immediate(float value):value(value){}
    float evaluate(ref<float>) const override { return value; }
    string str() const override { return ftoa(value); }
};
struct Parameter : Expression {
    uint index;
    Parameter(uint index):index(index){}
    float evaluate(ref<float> a) const override { if(index>=a.size) error("Missing argument"); return a[index]; }
    string str() const override { return "$"_+dec(index); }
};
struct Operator : Expression {
    byte op;
    unique<Expression> left,right;
    Operator(byte op, unique<Expression>&& left, unique<Expression>&& right):op(op),left(move(left)),right(move(right)){}
    float evaluate(ref<float> a) const override {
        if(!right) error("Missing right operand");
        float r=right->evaluate(a);
        if(op=='-' && !left) return -r;
        if(!left) error("Missing left operand");
        float l=left->evaluate(a);
        /**/  if(op=='=') return l==r;
        else if(op=='+') return l+r;
        else if(op=='-') return l-r;
        else if(op=='>') return l>r;
        else if(op=='*') return l*r;
        else if(op=='/') return l/r;
        else if(op=='^') return pow(l,r);
        else error("Unimplemented",op);
    }
    string str() const override { return left->str()+::str(op)+right->str(); }
};

/// Produced by a Rule
struct Production {
    word symbol;
    array<unique<Expression> > arguments;
    virtual Module operator()(ref<float> parameters) const {
        Module m(symbol);
        for(const unique<Expression>& e: arguments) m.arguments << e->evaluate(parameters);
        return m;
    }
    Production(const word& symbol):symbol(symbol){}
};
string str(const Production& o) { return o.arguments?string(str(o.symbol)+"("_+str(o.arguments,',')+")"_):copy(str(o.symbol)); }

/// Context-sensitive L-System rule
struct Rule {
    word edge;
    array<word> left; array<word> right;
    unique<Expression> condition = unique<Immediate>(true);
    array<Production> productions;
    Rule(const word& edge):edge(edge){}
};
string str(const Rule& o) { return /*str(o.left)+"<"_+*/str(o.edge)/*+">"_+str(o.right)*/+" → "_+str(o.productions); }

/// Bracketed, Stochastic, context-sensitive, parametric L-System definition parser and generator
struct LSystem {
    string name;
    array<Rule> rules;
    array<Module> axiom;
    map<string,float> constants;
    //ref<byte> ignore;

    unique<Expression> parse(const array<ref<byte> >& parameters, unique<Expression>&& e, TextData& s) const {
        s.skip();
        char c = s.peek();
        if(c>='0'&&c<='9') {
            return unique<Immediate>(s.decimal());
        } else if(c=='='||c=='+'||c=='-'||c=='>'||c=='*'||c=='/'||c=='^') {
            s.next();
            return unique<Operator>(c, move(e), parse(parameters,move(e),s));
        } else {
            ref<byte> identifier = s.whileNo("=+-><*/^(),:"_);
            int i = parameters.indexOf(identifier);
            if(i>=0) return unique<Parameter>(i);
            else if(constants.contains(string(identifier))) return unique<Immediate>(constants.at(string(identifier)));
            else error(name,"Unknown symbol",c,s.untilEnd(),constants);
        }
    }

    LSystem(){}
    LSystem(string&& name, const ref<byte>& source):name(move(name)){
        for(ref<byte> line: split(source,'\n')) {
            TextData s(line);
            s.skip();
            if(s.match('#')) continue;
            if(line.contains(':')) s.until(':'); s.skip();
            if(!s) continue;
            if(find(line,"←"_)) {
                ref<byte> name = trim(s.until("←"_));
                s.skip();
                unique<Expression> e;
                while(s) {
                    e = parse(array<ref<byte> >(),move(e),s);
                    s.skip();
                }
                assert(e);
                constants.insert(string(name)) = e->evaluate(ref<float>());
            } else if(find(line,"→"_)) {
                ref<byte> symbol = s.identifier();
                if(!symbol) error("Expected symbol",s.untilEnd());
                Rule rule(symbol);
                array<ref<byte>> parameters;
                if(s.match('(')) while(!s.match(')')) { parameters << s.identifier(); s.match(','); assert(s); }
                s.skip();
                if(s.match(':')) { // condition
                    s.skip();
                    if(!s.match('*')) {
                        unique<Expression> e;
                        while(!s.match(',') && s.peek("→"_.size)!="→"_) {
                            e = parse(parameters,move(e),s);
                            s.skip();
                            assert(s);
                        }
                        rule.condition = move(e);
                    }
                }
                s.skip();
                if(!s.match("→"_)) error("Expected → not \""_+s.untilEnd()+"\""_);
                s.skip();
                while(s) {
                    s.skip();
                    ref<byte> symbol = s.identifier("$-+&^%.![]\\/|{}"_);
                    if(!symbol) error("Expected symbol",s.untilEnd());
                    Production p(symbol);
                    s.skip();
                    if(s.match('(')) while(!s.match(')')) {
                        unique<Expression> e;
                        s.skip();
                        while(!s.match(',') && s.peek()!=')') {
                            e = parse(parameters,move(e),s);
                            assert(s);
                        }
                        p.arguments << move(e);
                    }
                    rule.productions << move(p);
                }
                rules << move(rule);
            } else {
                assert(!axiom);
                array<ref<byte>> parameters;
                for(;s;) {
                    s.skip();
                    ref<byte> symbol = s.identifier("$-+&^%.![]\\/|{}"_);
                    if(!symbol) error("Expected symbol",s.untilEnd());
                    Module module (symbol);
                    if(s.match('(')) while(!s.match(')')) {
                        unique<Expression> e;
                        while(!s.match(',') && s.peek()!=')') {
                            e = parse(parameters,move(e),s);
                            s.skip();
                            assert(s);
                        }
                        module.arguments << e->evaluate(ref<float>());
                    }
                    axiom << move(module);
                }
            }
        }
    }

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
                            array<word> path;
                            for(int j=i-1; path.size()<r.left.size() && j>=0; j--) {
                                const Module& c = code[j];
                                if(c=="]"_) { while(code[j]!="["_) j--; continue; } // skip brackets
                                if(c=="+"_ || c=="-"_ || c=="["_ /*|| ignore.contains(c)*/) continue;
                                path << c;
                                arguments << c.arguments;
                            }
                            if(path!=r.left) continue;
                        }
                        arguments << c.arguments;
                        if(r.right) {
                            array<word> path;
                            for(uint j=i+1; path.size()<r.right.size() && j<code.size(); j++) {
                                const Module& c = code[j];
                                if(c=="["_) { while(code[j]!="]"_) j++; continue; } // skip brackets
                                if(c=="+"_ || c=="-"_ /*|| ignore.contains(c)*/) continue;
                                path << c;
                                arguments << c.arguments;
                            }
                            if(path!=r.right) continue;
                        }
                        if(!r.condition->evaluate(arguments)) continue;
                        //if(!sensitive) matches.clear(), sensitive=true;
                        array<Module> modules;
                        for(const Production& production: r.productions) modules << production(c.arguments);
                        matches << modules;
                    } else {
                        //if(sensitive) continue;
                        if(!r.condition->evaluate(c.arguments)) continue;
                        array<Module> modules;
                        for(const Production& production: r.productions) modules << production(c.arguments);
                        matches << modules;
                        //}
                    }
                }
                    assert(matches.size()<=1);
                    //if(matches) next << copy(matches[random()%matches.size()]);
                    if(matches) next << copy(matches[0]);
                    else next << c;
            }
            debug( if(code==next) error("L-System stalled at level"_,i,code); )
            code = move(next);
        }
        return code;
    }
};

/// Generates and renders L-Systems
struct Editor : Widget {
    Window window __(this,int2(0,0),"L-System Editor"_);
    Folder folder = Folder(""_,cwd()); //L-Systems definitions are loaded from current working directory
    Bar<Text> systems; // Tab bar to select which L-System to view
    LSystem system; // Currently loaded L-System
    uint level=0; // Current L-System generation maximum level
    bool enableShadow=true; // Whether shadows are rendered
    int2 lastPos; // last cursor position to compute relative mouse movements
    vec2 rotation=vec2(PI,PI/4); // current view angles
    uint64 frameEnd=cpuTime(), frameTime=50000; // last frame end and initial frame time in microseconds
    profile( uint64 miscStart=rdtsc(); ) // profile cycles spent outside render
    RenderTarget target; // Render target (RenderPass renders on these tiles)

    // Scene
     vec3 sceneMin=0, sceneMax=0; // Scene bounding box
     /// Cone from A to B with radius wA to wB (stalk, branches)
     struct Cone { vec3 A,B; float wA,wB; vec3 color; };
     array<Cone> cones;
     /// Convex polygon (leaves)
     struct Polygon { array<vec3> vertices; vec3 color; };
     array<Polygon> polygons; uint triangleCount=0;

    /// Triggered by tab bar to load L-Systems
    void openSystem(uint index) {
        level=0;
        ref<byte> name = systems[index].text;
        system = LSystem(string(name), readFile(string(name+".l"_),folder));
        generate();
    }
    /// Generates the currently active L-System
    void generate() {
        cones.clear(); polygons.clear(); triangleCount=0; sceneMin=0, sceneMax=0;

        if(!level) level=system.constants.at(string("N"_));
        array<Module> modules = system.generate(level);

        // Turtle interpretation of modules string generated by an L-system
        array<mat4> stack; mat4 state; uint cut=0;
        array<Polygon> polygonStack; Polygon polygon;
        array<vec3> colorStack; vec3 color=vec3(1,1,1);
        for(const Module& module : modules) {
            word symbol = module.symbol;
            if(cut) { if(symbol=="["_) cut++; else if(symbol=="]"_) cut--; else continue; }
            if(symbol=="f"_ || symbol=="F"_ || symbol=="G"_) { // move forwards (optionally drawing a line)
                float l = module.arguments.size()>0?module.arguments[0]:4,
                        wA = module.arguments.size()>1?module.arguments[1]:1,
                        wB = module.arguments.size()>2?module.arguments[2]:1;
                vec3 A = state[3].xyz();
                state.translate(vec3(l,0,0)); //forward axis is +X
                vec3 B = state[3].xyz();
                if(symbol=="F"_) {
                    if(polygonStack) polygon.vertices << state[3].xyz();
                    else {
                        cones << Cone __(A,B,wA,wB,color);

                        // Apply tropism
                        if(system.constants.contains(string("tropism"_))) {
                            float tropism = system.constants.at(string("tropism"_));
                            vec3 X = state[0].xyz();
                            vec3 Y = cross(vec3(-1,0,0),X);
                            float y = length(Y);
                            if(y<0.01) continue; //X is colinear to tropism (all rotations are possible)
                            assert(Y.x==0);
                            state.rotate(tropism,state.inverse().normalMatrix()*Y);
                        }
                    }
                    sceneMin=min(sceneMin,B);
                    sceneMax=max(sceneMax,B);
                }
            } else if(symbol=="."_) polygon.vertices << state[3].xyz(); // Records a vertex in the current polygon
            else if(symbol=="["_) {
                stack << state; // pushes turtle (current position and orientation) on stack
                colorStack << color;
            } else if(symbol=="]"_) {
                state = stack.pop(); // pops turtle (current position and orientation) from stack
                color = colorStack.pop();
            } else if(symbol=="%"_) cut=1;
            else if(symbol=="{"_) { // pushes current polygon and starts a new one
                polygonStack << move(polygon);
                polygon.color = color;
            } else if(symbol=="}"_) { // stores current polygon and pop previous nesting level from stack
                polygons << move(polygon);
                polygon = polygonStack.pop();
            } else if(symbol=="!"_ || symbol=="\""_) { // change current color
                if(module.arguments.size()!=3) error("Expected !(b,g,r)");
                color = clip(vec3(0.f),vec3(module.arguments[0],module.arguments[1],module.arguments[2]),vec3(1.f));
            } else if(symbol=="$"_) { //set Y horizontal (keeping X), Z=X×Y
                            vec3 X = state[0].xyz();
                            vec3 Y = cross(vec3(1,0,0),X);
                            float y = length(Y);
                            if(y<0.01) continue; //X is colinear to vertical (all possible Y are already horizontal)
                            Y /= y;
                            assert(Y.x==0);
                            vec3 Z = cross(X,Y);
                            state[1] = vec4(Y,0.f);
                            state[2] = vec4(Z,0.f);
            } else if(symbol=="\\"_||symbol=="/"_||symbol=="&"_||symbol=="^"_||symbol=="-"_ ||symbol=="+"_) {
                float a = module.arguments ? module.arguments[0]*PI/180 : system.constants.at(string("angle"_))*PI/180;
                if(symbol=="\\"_||symbol=="/"_) state.rotateX(symbol=="\\"_?a:-a);
                else if(symbol=="&"_||symbol=="^"_) state.rotateY(symbol=="&"_?a:-a);
                else if(symbol=="-"_ ||symbol=="+"_) state.rotateZ(symbol=="+"_?a:-a);
            } else if(symbol=="|"_) state.rotateZ(PI);
        }
        for(const Polygon& polygon: polygons) if(polygon.vertices.size()>=3) triangleCount += polygon.vertices.size()-2;
        window.render();
    }

    /// Setups the application UI and shortcuts
    Editor() {
        window.localShortcut(Escape).connect(&::exit);
        window.fillBackground=0; //Disables background filling in Window::event

        // Scans current folder for L-System definitions (.l files)
        array<string> files = folder.list(Files);
        for(string& file : files) if(endsWith(file,".l"_)) systems << string(section(file,'.',0,-2));
        systems.activeChanged.connect(this,&Editor::openSystem);
        // Loads L-System
        systems.setActive(3);

        window.localShortcut(Key(KP_Sub)).connect([this]{if(level>0) level--; generate();});
        window.localShortcut(Key(KP_Add)).connect([this]{if(level<256) level++; generate();});
        window.localShortcut(Key(Return)).connect([this]{writeFile("snapshot.png"_,encodePNG(window.getSnapshot()),home());});
        window.localShortcut(Key(' ')).connect([this]{enableShadow=!enableShadow; window.render();});
    }

    /// Orbital view control
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override {
        if(systems.mouseEvent(cursor,int2(size.x,16),event,button)) return true; //Forward events to tabbar (custom layout)

        int2 delta = cursor-lastPos; lastPos=cursor;
        if(event==Motion && button==LeftButton) {
            rotation += float(2.f*PI)*vec2(delta)/vec2(size); //TODO: warp
            rotation.y= clip(0.f,rotation.y,float(PI/2)); // Keep pitch between [0,PI]
        } else return false;
        return true;
    }

    void render(int2 targetPosition, int2 targetSize) override {
        profile( uint64 setupStart=rdtsc(); uint64 miscTime = setupStart-miscStart; )

        if(targetSize != target.size) {
            target.resize(targetSize);
        } else {
            //RenderTarget::resolve regenerates framebuffer background color only as necessary, but UI rendering expects cleared framebuffer
            fill(targetPosition+Rect(targetSize.x,16),white,false);
            fill(targetPosition+int2(0,16)+Rect(256,256),white,false);
        }
        target.clear();

        // Fits view
        mat4 view; //Always fit using side view (avoid jarring auto zoom)
        vec3 viewMin=(view*sceneMin).xyz(), viewMax=(view*sceneMax).xyz();
        for(int x=0;x<2;x++) for(int y=0;y<2;y++) for(int z=0;z<2;z++) {
            viewMin = min(viewMin,(view*vec3((x?sceneMax:sceneMin).x,(y?sceneMax:sceneMin).y,(z?sceneMax:sceneMin).z)).xyz());
            viewMax = max(viewMax,(view*vec3((x?sceneMax:sceneMin).x,(y?sceneMax:sceneMin).y,(z?sceneMax:sceneMin).z)).xyz());
        }
        vec2 sceneSize = (viewMax-viewMin).xy();
        vec2 displaySize = vec2(targetSize.x*4,targetSize.y*4);
        float scale = min(displaySize.x/sceneSize.x,displaySize.y/sceneSize.y) * 0.75;
        view = mat4();
        view.translate(vec3(displaySize.x/2,displaySize.y/4,0.f)); //center scene
        view.scale(scale); //normalize view space to fit display
        view.rotateX(rotation.y); // pitch
        view.rotateY(rotation.x); // yaw
        view.rotateZ(PI/2); //+X (heading) is up
        mat3 normalMatrix = view.normalMatrix();

        vec3 skyLightDirection = normalize(normalMatrix*vec3(1,0,0));

        // Fits light
        mat4 sun; sun.rotateY(PI/4);
        vec3 lightMin=(sun*sceneMin).xyz(), lightMax=(sun*sceneMax).xyz();
        for(int x=0;x<2;x++) for(int y=0;y<2;y++) for(int z=0;z<2;z++) {
            lightMin = min(lightMin,(sun*vec3((x?sceneMax:sceneMin).x,(y?sceneMax:sceneMin).y,(z?sceneMax:sceneMin).z)).xyz());
            lightMax = max(lightMax,(sun*vec3((x?sceneMax:sceneMin).x,(y?sceneMax:sceneMin).y,(z?sceneMax:sceneMin).z)).xyz());
        }
        sun=mat4();
        float lightScale = scale/64.f;
        sun.scale(lightScale);
        sun.translate(-lightMin);
        sun.rotateY(PI/4);
        vec2 size = lightScale*(lightMax-lightMin).xy();
        mat4 viewToSun = sun*view.inverse();
        mat4 sunToView= view*sun.inverse();
        vec3 sunLightDirection = normalize(sunToView.normalMatrix()*vec3(0,0,-1));

        // Holds scene geometry projected in light space bins
        Shadow sunShadow(size, cones.size()*2 + triangleCount);


        // Setups branches (view-aligned rectangle impostors)
        ConeShader coneShader (sunShadow);
        coneShader.enableShadow=enableShadow, coneShader.sky = skyLightDirection, coneShader.sun =sunLightDirection;
        RenderPass<ConeShader> conePass __(target, cones.size()*2, coneShader);
        for(Cone cone: cones) {
            // View transform
            vec3 A=(view*cone.A).xyz(); float wA = scale*cone.wA;
            vec3 B=(view*cone.B).xyz(); float wB = scale*cone.wB;

            // Principal axis
            vec3 X = B-A; {float l = length(X);  if(l<1) continue; X /= l;}

            // Normal basis to be interpolated
            vec3 Y=normalize(cross(vec3(0,0,1),X)), Z=cross(X,Y);
            // Normal in light space (scaled with radius) to fix light position
            vec3 N = normalize(viewToSun.normalMatrix()*Z)*lightScale*max(cone.wA,cone.wB);

            // View aligned rectangle
            vec4 a(A - wA*Y, 1.f), b(B - wB*Y, 1.f), c(B + wB*Y, 1.f), d(A + wA*Y, 1.f);
            // View aligned rectangle position in light space (interpolate light space coordinates)
            vec4 as = viewToSun*a, bs = viewToSun*b, cs=viewToSun*c, ds=viewToSun*d;
            vec3 attributes[][4] = {
                {vec3(-1,-1,1),vec3(as.x,bs.x,cs.x),vec3(as.y,bs.y,cs.y),vec3(as.z,bs.z,cs.z)},
                {vec3(1,1,-1),vec3(cs.x,ds.x,as.x),vec3(cs.y,ds.y,as.y),vec3(cs.z,ds.z,as.z)}
            };
            conePass.submit(a,b,c,attributes[0],__(Y,Z,N,cone.color));
            conePass.submit(c,d,a,attributes[1],__(Y,Z,N,cone.color));

            if(enableShadow) { // Light aligned rectangle (for shadows)
                float wA = lightScale*cone.wA; vec3 A=(sun*cone.A).xyz(); //-vec3(0,0,2*wA); //offset to prevent self shadowing
                float wB = lightScale*cone.wB; vec3 B=(sun*cone.B).xyz(); //-vec3(0,0,2*wB);
                vec3 X = B-A;  if(length(X.xy())<0.01) continue;
                vec3 Y = normalize(cross(vec3(0,0,1),X));
                vec4 a(A - wA*Y, 1.f), b(B - wB*Y, 1.f), c(B + wB*Y, 1.f), d(A + wA*Y, 1.f);
                sunShadow.submit(a,b,c);
                sunShadow.submit(c,d,a);
            }
        }

        // Setups leafs (convex polygons)
        FlatShader flatShader (sunShadow);
        flatShader.enableShadow=enableShadow, flatShader.sky = skyLightDirection, flatShader.sun =sunLightDirection;
        RenderPass<FlatShader> flatPass __(target, 2*triangleCount, flatShader);
        for(const Polygon& polygon: polygons) {
            if(polygon.vertices.size()<3) continue;
            vec3 A = polygon.vertices.first();
            vec3 next = polygon.vertices[1];
            for(vec3 current: polygon.vertices.slice(2)) {
                vec3 B = next, C = current;
                vec3 N = normalize(normalMatrix*cross(B-A,C-A));
                vec4 as = sun*A, bs = sun*B, cs=sun*C;
                if(cross((bs-as).xyz(),(cs-as).xyz()).z < 0) sunShadow.submit(cs,bs,as);
                else sunShadow.submit(as,bs,cs);
                {
                    vec3 lightPos[3] = {vec3(as.x,bs.x,cs.x),vec3(as.y,bs.y,cs.y),vec3(as.z,bs.z,cs.z)};
                    flatPass.submit(view*A,view*B,view*C, lightPos, __(N,polygon.color));
                }
                {
                    vec3 lightPos[3] = {vec3(cs.x,bs.x,as.x),vec3(cs.y,bs.y,as.y),vec3(cs.z,bs.z,as.z)};
                    flatPass.submit(view*C,view*B,view*A, lightPos, __(-N,polygon.color));
                }
                next = current;
            }
        }

        // Actual rendering happens there
        profile( uint64 renderStart=rdtsc(); uint64 setupTime = renderStart-setupStart; )
        flatPass.render();
        conePass.render();
        profile( uint64 resolveStart=rdtsc(); uint64 renderTime = resolveStart-renderStart; )

        // Shadow-receiving ground plane
        if(enableShadow) {
            // Fit size to receive all shadows
            mat4 sunToWorld = sun.inverse();
            lightMin=vec3(0,0,0), lightMax=vec3(sunShadow.width,sunShadow.height,0);
            vec3 lightRay = sunToWorld.normalMatrix()*vec3(0,0,1);
            float xMin=lightMin.x, xMax=lightMax.x, yMin=lightMin.y, yMax=lightMax.y;
            vec4 a = sunToWorld*vec3(xMin,yMin,0),
                    b = sunToWorld*vec3(xMin,yMax,0),
                    c = sunToWorld*vec3(xMax,yMax,0),
                    d = sunToWorld*vec3(xMax,yMin,0);
            // Project light view corners on ground plane
            float dotNL = dot(lightRay,vec3(1,0,0));
            a -= vec4(lightRay*dot(a.xyz(),vec3(1,0,0))/dotNL,0.f);
            b -= vec4(lightRay*dot(b.xyz(),vec3(1,0,0))/dotNL,0.f);
            c -= vec4(lightRay*dot(c.xyz(),vec3(1,0,0))/dotNL,0.f);
            d -= vec4(lightRay*dot(d.xyz(),vec3(1,0,0))/dotNL,0.f);
            a=view*a; b=view*b; c=view*c; d=view*d;
            vec4 as = viewToSun*a, bs = viewToSun*b, cs=viewToSun*c, ds=viewToSun*d;
            vec3 attributes[][4] = {
                {vec3(as.x,bs.x,cs.x),vec3(as.y,bs.y,cs.y),vec3(as.z,bs.z,cs.z)},
                {vec3(cs.x,ds.x,as.x),vec3(cs.y,ds.y,as.y),vec3(cs.z,ds.z,as.z)}
            };
            ShadowShader groundShader (sunShadow);
            RenderPass<ShadowShader> groundPass __(target, 2, groundShader);
            groundPass.submit(a,b,c,attributes[0],__());
            groundPass.submit(c,d,a,attributes[1],__());
            groundPass.render();
        }

        target.resolve(targetPosition,targetSize); // Resolves RenderTarget to display

        profile( uint64 uiStart = rdtsc(); uint64 resolveTime = uiStart-resolveStart;
                    uint64 totalTime = miscTime+setupTime+renderTime+resolveTime+uiTime; )
        uint frameEnd = cpuTime(); frameTime = ( (frameEnd-this->frameEnd) + (64-1)*frameTime)/64; this->frameEnd=frameEnd;

        // Displays user interface
        systems.render(targetPosition,int2(targetSize.x,16));
        Text(str(level)+" "_+ftoa(1e6f/frameTime,1)+"fps "_+str(frameTime/1000)+"ms "_+str(cones.size()*2 + triangleCount*2)+" faces\n"
                       profile(
                       "misc "_+str(100*miscTime/totalTime)+"%\n"
                       "setup "_+str(100*setupTime/totalTime)+"%\n"
                       "render "_+str(100*renderTime/totalTime)+"%\n"
                       "- raster "_+str(100*pass.rasterTime/totalTime)+"%\n"
                       "- pixel "_+str(100*(pass.pixelTime)/totalTime)+"%\n"
                       "- sample "_+str(100*(pass.sampleTime)/totalTime)+"%\n"
                       "-- MSAA split "_+str(100*(pass.sampleFirstTime)/totalTime)+"%\n"
                       "-- MSAA over "_+str(100*(pass.sampleOverTime)/totalTime)+"%\n"
                       "- user "_+str(100*pass.userTime/totalTime)+"%\n"
                       "resolve "_+str(100*resolveTime/totalTime)+"%\n"
                           "ui "_+str(100*uiTime/totalTime)+"%\n") ""_).render(int2(targetPosition+int2(16)));
        profile( miscStart = rdtsc(); uiTime = ( (miscStart-uiStart) + (T-1)*uiTime)/T; )
#if 0
        window.render(); //keep updating to get maximum performance profile
#endif
    }
} application;
