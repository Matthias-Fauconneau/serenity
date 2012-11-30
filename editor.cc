/** \file editor.cc
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
#include "window.h"
#include "interface.h"
#include "text.h"
#include "time.h"
#include "png.h"
#include "filewatcher.h"
#include "lsystem.h"
#include "gl.h"

#if 0
// Light

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

/// Shadow sampling patterns
static struct Pattern {
    vec16 X,Y;
    Pattern() {
        // jittered radial sample pattern (TODO: spatial [+temporal?] filter)
        Random random;
        for(int u=0;u<4;u++) for(int v=0;v<4;v++) {
            float r = sqrt( (u + (random+1)/2.f) / 4.f );
            float t = (v + (random+1)/2.f) / 4.f;
            X[u*4+v] = r*cos(2*PI*t);
            Y[u*4+v] = r*sin(2*PI*t);
        }
    }
} patterns[16];

/// Stores shadow occluders for shadow-receiving surfaces
struct Shadow {
    struct Face {
        mat3 E; //edge functions
        vec3 iw, iz;
    };
    // Occluders geometry (in light space)
    Face* faces=0;
    uint faceCapacity=0;
    uint faceCount=0;

    // 2D space partitioning to accelerate shadow ray casts
    struct Bin { uint16 faceCount=0; uint16 faces[63]; };
    int width=0, height=0; float maxZ=0;
    Bin* bins=0;

    // Randomly select one of the precomputed jittered patterns
    Random randomPattern;

    // Resizes light space
    void setup(int2 size, float maxZ, uint faceCapacity) {
        if(bins) unallocate(bins,width*height);
        if(faces) unallocate(faces,faceCapacity);
        width = size.x, height = size.y; this->maxZ=maxZ;
        bins = allocate64<Bin>(width*height);
        faces = allocate64<Face>(this->faceCapacity=faceCapacity);
        for(uint i: range(width*height)) bins[i].faceCount=0;
        faceCount = 0;
    }
    ~Shadow() {
        if(bins) unallocate(bins,width*height);
        if(faces) unallocate(faces,faceCapacity);
    }

    // Scales sample pattern to soften shadow depending on occluder distance
    float A0=1; // pixel area in light space
    float dzA=1/256.f; // tan(light angular diameter)

    // Setups a shadow-casting face
    void submit(vec4 A, vec4 B, vec4 C) {
        assert(abs(A.w-1)<0.01); assert(abs(B.w-1)<0.01); assert(abs(C.w-1)<0.01);
        if(faceCount>=faceCapacity) { userError("Face overflow"_); return; }

        Face& face = faces[faceCount];
        mat3& E = face.E;
        E = mat3(A.xyw(), B.xyw(), C.xyw());
        E = E.cofactor(); //edge equations are now columns of E

        face.iw = E[0]+E[1]+E[2];
        face.iz = E*vec3(A.z,B.z,C.z);
        float meanZ = (A.z+B.z+C.z)/3;

        float binReject[3];
        for(int e=0;e<3;e++) {
            const vec2& edge = E[e].xy();
            float step0 = (edge.x>0?edge.x:0) + (edge.y>0?edge.y:0); //conserve whole bin
            float step1 = abs(edge.x) + abs(edge.y); //add margin for soft shadow sampling
            // initial reject corner distance
            float d = maxZ-meanZ;
            float spread = A0+dzA*d;
            binReject[e] = E[e].z + step0 + spread*step1;
        }

        int2 min = ::max(int2(1,1),int2(floor(::min(::min(A.xy(),B.xy()),C.xy()))));
        int2 max = ::min(int2(width-1-1,height-1-1),int2(ceil(::max(::max(A.xy(),B.xy()),C.xy()))));
        for(int binY=min.y-1; binY<=max.y+1; binY++) for(int binX=min.x-1; binX<=max.x+1; binX++) {
            const vec2 binXY = vec2(binX, binY);
            // trivial reject
            if(
                    binReject[0] + dot(E[0].xy(), binXY) <= 0 ||
                    binReject[1] + dot(E[1].xy(), binXY) <= 0 ||
                    binReject[2] + dot(E[2].xy(), binXY) <= 0) continue;
            Bin& bin = bins[binY*width+binX];
            if(bin.faceCount>=sizeof(bin.faces)/sizeof(uint16)) { userError("Index overflow"); return; }
            bin.faces[bin.faceCount++]=faceCount;
        }
        faceCount++;
    }

    // Computes light occlusion
    inline float operator()(vec3 lightPos) {
        const Pattern& pattern = patterns[randomPattern()%16]; // FIXME: race
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
                if(d<=0.f) continue; //only Z-test midpoint

                float spread = A0+dzA*d;
                vec16 lightX = lightPos.x + spread*pattern.X, lightY = lightPos.y + spread*pattern.Y;

                vec16 a = face.E[0].x * lightX + face.E[0].y * lightY + face.E[0].z;
                vec16 b = face.E[1].x * lightX + face.E[1].y * lightY + face.E[1].z;
                vec16 c = face.E[2].x * lightX + face.E[2].y * lightY + face.E[2].z;
                samples = samples | ((a > 0.f) & (b > 0.f) & (c > 0.f));
            }
        }
        return __builtin_popcount(mask(samples))/16.f;
    }

    // Reports profiling information
    string depthComplexity() {
        uint sum=0, N=0, max=0;
        for(int binY=0; binY<height; binY++) for(int binX=0; binX<width; binX++) {
            uint faceCount = bins[binY*width+binX].faceCount;
            if(faceCount) N++, sum += faceCount, max=::max(max,faceCount);
        }
        return N ? str(sum / N, max) : string("-"_);
    }
};

// Shaders

/// Shader for flat surfaces (e.g leaves) lit by hemispheric sky and shadow-casting sun lights.
struct Shader {
    // Shader specification (used by rasterizer)
    struct FaceAttributes {
        vec3 albedo; // Surface color  //TODO: vertex interpolated
    };
    static constexpr int V = 6; // number of interpolated vertex attributes (3D light space position, 3D view-space normal)
    static constexpr bool blend=false; // Disable unecessary blending

    // Uniform attributes
    static constexpr vec3 skyColor = vec3(1./2,1./4,1./8);
    static constexpr vec3 sunColor = vec3(1./2,3./4,7./8);
    vec3 sky, sun; // View-space sky and sun light direction
    bool enableShadow; // Whether shadow are casts
    Shadow& shadow; // Occluders

    Shader(Shadow& shadow):shadow(shadow){} //FIXME: inherit constructor

    // Computes diffused light from hemispheric sky light and shadow-casting directionnal sun light
    vec4 operator()(FaceAttributes face, float varying[V]) const {
        vec3 N = normalize(vec3(varying[0],varying[1],varying[2]));

        float sunLight=1.f;
        if(enableShadow) {
            vec3 lightPos = vec3(varying[3],varying[4],varying[5]);
            sunLight -= shadow(lightPos);
        }

        vec3 diffuseLight = hemisphericLight(N,sky)*skyColor + sunLight*directionnalLight(N,sun)*sunColor;
        vec3 diffuse = face.albedo*diffuseLight;
        return vec4(diffuse,1.f);
    }
};
#endif

/// Flat shaded face to be submitted to rasterizer
struct Face {
    vec3 position[3]; // World-space positions
    vec3 normal[3]; // World-space vertex normals
    vec3 color[3]; // BGR albedo (TODO: texture mapping)
};

/// Generates and renders L-Systems
struct Editor : Widget {
    // L-System
    Folder folder = Folder(""_,cwd()); //L-Systems definitions are loaded from current working directory
    FileWatcher watcher; // Watch the loaded definition for changes
    LSystem system; // Currently loaded L-System
    uint level=0; // Current L-System generation level

    // View
    bool enableShadow=true; // Whether shadows are rendered
    int2 lastPos; // last cursor position to compute relative mouse movements
    vec2 rotation=vec2(0,PI/4); // current view angles (yaw,pitch)
    float scale=1; // current view scale
    mat4 sun; // sun light transform (TODO: interactive)

    // User interface
    Bar<Text> systems; // Tab bar to select which L-System to view
    VBox layout;
    TextInput editor;
    Window window __(&layout,int2(0,0),"L-System Editor"_);
    GLContext context __(window);

    // Renderer
    //Shadow sunShadow; // Holds scene geometry projected in light space bins
    GLBuffer buffer;
    SHADER(shader);

    // Scene
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
    vec3 worldCenter=0; float worldRadius=0; // Scene bounding sphere in world space
    vec3 lightMin=0, lightMax=0; // Scene bounding box in light space
    float lightScale=0;
    array<Face> faces; // Generated scene geometry data

     /// Setups the application UI and shortcuts
     Editor() {
         layout << &systems << this << &editor;
         window.localShortcut(Escape).connect(&::exit);

         // Scans current folder for L-System definitions (.l files)
         array<string> files = folder.list(Files);
         for(string& file : files) if(endsWith(file,".l"_)) systems << section(file,'.',0,-2);
         systems.activeChanged.connect(this,&Editor::openSystem);
         // Loads L-System
         systems.setActive(8);

         window.localShortcut(Key(KP_Sub)).connect([this]{if(level>0) level--; generate();});
         window.localShortcut(Key(KP_Add)).connect([this]{if(level<256) level++; generate();});
         window.localShortcut(Key(Return)).connect([this]{writeFile("snapshot.png"_,encodePNG(window.getSnapshot()),home());});
         window.localShortcut(Key(' ')).connect([this]{enableShadow=!enableShadow; window.render();});
         watcher.fileModified.connect([this]{openSystem(systems.index);});
         editor.textChanged.connect([this](const ref<byte>& text){writeFile(string(system.name+".l"_),text,cwd());});
     }

     // Orbital view control
     bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override {
         int2 delta = cursor-lastPos; lastPos=cursor;
         if(event==Motion && button==LeftButton) {
             rotation += float(2.f*PI)*vec2(delta)/vec2(size); //TODO: warp
             rotation.y= clip(float(-PI/2),rotation.y,float(PI/2)); // Keep pitch between [-PI/2,PI/2]
         }
         else if(event == Press) focus = this;
         else return false;
         return true;
     }

    /// Triggered by tab bar to load L-Systems
    void openSystem(uint index) {
        level=0;
        string name = toUTF8(systems[index].text);
        string path = name+".l"_;
        watcher.setPath(path);
        string source = readFile(path, folder);
        system = LSystem(move(name), source);
        editor.setText(move(source));
        generate();
    }
    /// Generates a scene showing the currently active L-System
    void generate() {
        if(system.parseErrors) return;

        // Clears previous scene
        faces.clear(); worldMin=0, worldMax=0, worldCenter=0, worldRadius=0, lightMin=0, lightMax=0;
        // Generates new L-system
        if(!level) level=system.constants.at(string("N"_));
        array<Module> modules = system.generate(level);

        // Turtle interpretation of modules string generated by an L-system
        uint cut=0;
        array<mat4> stack; mat4 state;
        array<mat4> stackB; mat4 stateB; // keep end point stack for seamless junctions
        struct Polygon { array<vec3> vertices; vec3 color; };
        array<Polygon> polygonStack; Polygon polygon;
        array<vec3> colorStack; vec3 color=vec3(1,1,1);
        for(const Module& module : modules) {
            word symbol = module.symbol;
            if(cut) { if(symbol=="["_) cut++; else if(symbol=="]"_) cut--; else continue; }
            if(symbol=="f"_ || symbol=="F"_ || symbol=="G"_) { // move forwards (optionally drawing a line)
                float l = module.arguments.size()>0?module.arguments[0]:4,
                        wA = module.arguments.size()>1?module.arguments[1]:1,
                        wB = module.arguments.size()>2?module.arguments[2]:1;
                mat4 A = stateB; //seamless branch junctions
                A.scale(wA);
                // Move forward
                state.translate(vec3(l,0,0)); //forward axis is +X
                if(symbol=="F"_) { // Apply tropism
                    if(system.constants.contains(string("tropism"_))) {
                        float tropism = system.constants.at(string("tropism"_));
                        vec3 X = state[0].xyz();
                        vec3 Y = cross(vec3(-1,0,0),X);
                        float y = length(Y);
                        if(y>0.01) { //only if X is not colinear with tropism vector
                            assert(Y.x==0);
                            state.rotate(tropism,state.inverse().normalMatrix()*Y);
                        }
                    }
                }
                mat4 B = stateB = state;
                B.scale(wB);
                if(symbol=="F"_) {
                    vec3 P = state[3].xyz();
                    if(polygonStack) polygon.vertices << P;
                    else { // Tesselated branch (TODO: indexing)
                        array<Face> faces;
                        vec3 a = (A*vec3(0,1,0)).xyz() - A[3].xyz(), b = (B*vec3(0,1,0)).xyz() - B[3].xyz();
                        const int Na = max(3,int(length(a))), Nb = max(3,int(length(b)));
                        for(int i=0;i<Na;i++) {
                            float ta0 = 2*PI*i/Na, ta1 = 2*PI*(i+1)/Na;
                            vec3 a0 = (A * (vec3(0, cos(ta0), sin(ta0)))).xyz();
                            vec3 a1 = (A * (vec3(0, cos(ta1), sin(ta1)))).xyz();
                            int bestJ=-1; float bestD=__FLT_MAX__;
                            for(int j=0;j<Na;j++) { //FIXME: find out the smarter way to solve this
                                float tb0 = 2*PI*j/Nb, tb1 = 2*PI*(j+1)/Nb;
                                vec3 b0 = (B * (vec3(0, cos(tb0), sin(tb0)))).xyz();
                                vec3 b1 = (B * (vec3(0, cos(tb1), sin(tb1)))).xyz();
                                vec3 a = A[3].xyz(), b = B[3].xyz();
                                float d = length((a0-a)-(b0-b))+length((a1-a)-(b1-b));
                                if(d<bestD) bestJ=j, bestD=d;
                            }
                            float tb0 = 2*PI*bestJ/Nb, tb1 = 2*PI*(bestJ+1)/Nb;
                            vec3 b0 = (B * (vec3(0, cos(tb0), sin(tb0)))).xyz();
                            vec3 b1 = (B * (vec3(0, cos(tb1), sin(tb1)))).xyz();
                            if(isNaN(a0) || isNaN(a1) || isNaN(b0) || isNaN(b1)) { userError("NaN"); continue; }
                            {vec3 N = normalize(cross(a1-a0,b0-a0)); faces << Face __({a0,a1,b0},{N,N,N}, {color,color,color}); }
                            {vec3 N = normalize(cross(b1-a1,b0-a1)); faces << Face __({a1,b1,b0},{N,N,N}, {color,color,color}); }
                        }

                        // Computes smoothed normals (weighted by triangle areas)
                        for(Face& face: faces) for(uint i: range(3)) {
                            vec3 N=0; vec3 O = face.position[i];
                            for(const Face& face: faces) for(const vec3& P : ref<vec3>__(face.position, 3)) {
                                if(length(P-O)<1) {
                                    N += cross(face.position[1]-face.position[0],face.position[2]-face.position[0]);
                                }
                            }
                            face.normal[i] = normalize(N);
                        }
                        this->faces << faces;
                    }
                }
            } else if(symbol=="."_) {
                vec3 P = state[3].xyz();
                polygon.vertices << P; // Records a vertex in the current polygon
            } else if(symbol=="["_) {
                stack << state; // pushes turtle (current position and orientation) on stack
                stackB << stateB;
                colorStack << color;
            } else if(symbol=="]"_) {
                if(!stack) { userError("Unbalanced []"); return; }
                state = stack.pop(); // pops turtle (current position and orientation) from stack
                stateB = stackB.pop();
                color = colorStack.pop();
            } else if(symbol=="%"_) cut=1;
            else if(symbol=="{"_) { // pushes current polygon and starts a new one
                polygonStack << move(polygon);
                polygon.color = color;
            } else if(symbol=="}"_) { // stores current polygon and pop previous nesting level from stack
                if(polygon.vertices.size()>=3) {
                    vec3 A = polygon.vertices.first();
                    vec3 B = polygon.vertices[1];
                    for(vec3 C: polygon.vertices.slice(2)) {
                        vec3 N = cross(B-A,C-A);
                        float area = length(N);
                        if(area<0.01) { userError("degenerate"); continue; } //discard degenerate triangles
                        N /= area;
                        vec3 dn = 0.1f*N; // avoid self-shadowing
                        if(isNaN(A) || isNaN(B) || isNaN(C)) { userError("NaN"); continue; }
                        faces << Face __({A+dn, B+dn, C+dn}, {N,N,N}, {polygon.color,polygon.color,polygon.color});
                        faces << Face __({C-dn, B-dn, A-dn}, {-N,-N,-N}, {polygon.color,polygon.color,polygon.color}); //Two-sided
                        B = C;
                    }
                }
                polygon = polygonStack.pop();
            } else if(symbol=="!"_ || symbol=="\""_) { // change current color
                if(module.arguments.size()!=3) userError("Expected !(b,g,r)");
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
                float a = module.arguments ? module.arguments[0]*PI/180 : system.constants.value(string("angle"_),90)*PI/180;
                if(symbol=="\\"_||symbol=="/"_) state.rotateX(symbol=="\\"_?a:-a);
                else if(symbol=="&"_||symbol=="^"_) state.rotateY(symbol=="&"_?a:-a);
                else if(symbol=="-"_ ||symbol=="+"_) state.rotateZ(symbol=="+"_?a:-a);
            } else if(symbol=="|"_) state.rotateZ(PI);
        }

        // Compute scene bounds (in world space to fit view, and in light space to fit shadow)
        sun=mat4(); sun.rotateY(PI/4);
        for(const Face& face: faces) for(const vec3& P : ref<vec3>__(face.position, 3)) {
            worldMin=min(worldMin,P);
            worldMax=max(worldMax,P);
            vec3 lightP = (sun*P).xyz();
            lightMin=min(lightMin,lightP);
            lightMax=max(lightMax,lightP);
        }
        worldCenter = (worldMax+worldMin)/2.f; worldRadius=length(worldMax-worldMin)/2.f; //FIXME: compute smallest enclosing sphere

        // Fit size to receive all shadows
        mat4 sunToWorld = sun.inverse();
        vec3 lightRay = sunToWorld.normalMatrix()*vec3(0,0,1);
        float xMin=lightMin.x, xMax=lightMax.x, yMin=lightMin.y, yMax=lightMax.y;
        vec3 A = (sunToWorld*vec3(xMin,yMin,0)).xyz(),
                B = (sunToWorld*vec3(xMin,yMax,0)).xyz(),
                C = (sunToWorld*vec3(xMax,yMax,0)).xyz(),
                D = (sunToWorld*vec3(xMax,yMin,0)).xyz();
        // Project light view corners on ground plane
        float dotNL = dot(lightRay,vec3(1,0,0));
        A -= lightRay*dot(A,vec3(1,0,0))/dotNL;
        B -= lightRay*dot(B,vec3(1,0,0))/dotNL;
        C -= lightRay*dot(C,vec3(1,0,0))/dotNL;
        D -= lightRay*dot(D,vec3(1,0,0))/dotNL;
        // Add ground plane to scene
        vec3 N = vec3(1,0,0);
        faces << Face __ ({A,B,C},{N,N,N},{vec3(1,1,1),vec3(1,1,1),vec3(1,1,1)});
        faces << Face __ ({A,C,D},{N,N,N},{vec3(1,1,1),vec3(1,1,1),vec3(1,1,1)});
        // Update light bounds (Z far)
        { vec3 lightP = (sun*A).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }
        { vec3 lightP = (sun*B).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }
        { vec3 lightP = (sun*C).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }
        { vec3 lightP = (sun*D).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }

        // Submits geometry


        window.render();
    }

    uint64 frameEnd=cpuTime(), frameTime=180000; // last frame end and initial frame time in microseconds

    void render(int2 position, int2 size) override {
        // Computes view transform
        mat4 view;
        view.scale(vec3(1.f/size.x, 1.f/size.y, 1.f));
        view.translate(vec3(size.x/2,size.y/3,0)); //center origin
        float scale = this->scale*max(size.x,size.y)/worldRadius/2; //normalize view space to fit display
        view.scale(scale);
        view.rotateX(rotation.y); // yaw
        view.rotateY(rotation.x); // pitch
        view.rotateZ(PI/2); //+X (heading) is up
        // View-space lighting
        //mat3 normalMatrix = view.normalMatrix();
        //vec3 sunLightDirection = normalize((view*sun.inverse()).normalMatrix()*vec3(0,0,-1));
        //vec3 skyLightDirection = normalize(normalMatrix*vec3(1,0,0));

#if 0
        // Setups sun shadow
        float A0 = scale/4; //scales shadow sampling to view pixel
        if(A0 != sunShadow.A0) {
            sunShadow.A0 = A0;
            sun=mat4();
            float lightScale = A0/2;
            sun.scale(lightScale); // Bins the polygon
            sun.translate(-lightMin);
            sun.rotateY(PI/4);

            vec3 lightSpace = lightScale*(lightMax-lightMin);
            int2 sunShadowSize = int2(ceil(lightSpace.xy()));
            sunShadow.setup(sunShadowSize, lightSpace.z, faces.size()); //FIXME: incorrect Zmax (ground plane receiver is not in lightSpace)

            // Setups shadow casting faces (TODO: vectorize)
            for(Face& face: faces) {
                vec4 a = sun*face.position[0], b = sun*face.position[1], c=sun*face.position[2];
                if(cross((b-a).xyz(),(c-a).xyz()).z > 0) { //backward cull
                    sunShadow.submit(a,b,c);
                } else {
                    // Precomputes light space position to be interpolated as a vertex attribute
                    face.lightPosition[0] = vec3(a.x,b.x,c.x);
                    face.lightPosition[1] = vec3(a.y,b.y,c.y);
                    face.lightPosition[2] = vec3(a.z,b.z,c.z);
                }
            }
        }
#endif

        glViewport(position, size);
        glClear();

        context.swapBuffers();
        uint frameEnd = cpuTime(); frameTime = ( (frameEnd-this->frameEnd) + (16-1)*frameTime)/16; this->frameEnd=frameEnd;

        // Overlays errors / profile information (FIXME: software rendered overlay)
        Text(system.parseErrors ? copy(userErrors) :
                                  userErrors ? move(userErrors) :
                                               ftoa(1e6f/frameTime,1)+"fps "_+str(frameTime/1000)+"ms "_+str(faces.size())+" faces\n"_)
                .render(int2(position+int2(16)));
#if 0
        window.render(); //keep updating to get maximum performance profile
#endif
    }
} application;
