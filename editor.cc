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
#include "data.h" //cast

struct Vertex {
    vec3 position; // World-space position
    vec3 color; // BGR albedo (TODO: texture mapping)
    vec3 normal; // World-space vertex normals
};
struct Face { Vertex vertices[3]; };

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
    mat4 sun; // sun light transform (TODO: interactive)

    // User interface
    Bar<Text> systems; // Tab bar to select which L-System to view
    TextInput editor;
    VBox layout;
    Window window __(&layout, int2(0,0), "L-System Editor"_);

    // Renderer
    GLFrameBuffer sunShadow;
    GLFrameBuffer framebuffer;
    GLBuffer buffer;
    SHADER(shader) GLShader& shader = shaderShader();
    SHADER(resolve) GLShader& resolve = resolveShader();
    //Shadow sunShadow; // Holds scene geometry projected in light space bins

    // Scene
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
    vec3 worldCenter=0; float worldRadius=0; // Scene bounding sphere in world space
    vec3 lightMin=0, lightMax=0; // Scene bounding box in light space
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
                            {vec3 N = normalize(cross(a1-a0,b0-a0)); faces << Face __({{a0,color,N},{a1,color,N},{b0,color,N}}); }
                            {vec3 N = normalize(cross(b1-a1,b0-a1)); faces << Face __({{a1,color,N},{b1,color,N},{b0,color,N}}); }
                        }

                        // Computes smoothed normals (weighted by triangle areas)
                        for(Face& face: faces) for(uint i: range(3)) {
                            vec3 N=0; vec3 O = face.vertices[i].position;
                            for(const Face& face: faces) for(const Vertex& V : ref<Vertex>__(face.vertices, 3)) {
                                if(length(V.position-O)<1) {
                                    N += cross(face.vertices[1].position-face.vertices[0].position,face.vertices[2].position-face.vertices[0].position);
                                }
                            }
                            face.vertices[i].normal = normalize(N);
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
                        faces << Face __({{A+dn,polygon.color,N},{B+dn,polygon.color,N},{C+dn,polygon.color,N}});
                        faces << Face __({{C-dn,polygon.color,-N},{B-dn,polygon.color,-N},{A-dn,polygon.color,-N}}); //Two-sided
                        B = C;
                    }
                }
                polygon = polygonStack.pop();
            } else if(symbol=="!"_ || symbol=="\""_) { // change current color
                if(module.arguments.size()!=3) userError("Expected !(b,g,r)");
                color = clip(vec3(0.f),vec3(module.arguments[2],module.arguments[1],module.arguments[0]),vec3(1.f));
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

        // Compute scene bounds in light space to fit shadow
        sun=mat4(); sun.rotateY(PI/4);
        for(const Face& face: faces) for(const Vertex& V : ref<Vertex>__(face.vertices, 3)) {
            vec3 P = (sun*V.position).xyz();
            lightMin=min(lightMin,P);
            lightMax=max(lightMax,P);
        }

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
        faces << Face __ ({{A,vec3(1,1,1),N},{B,vec3(1,1,1),N},{C,vec3(1,1,1),N}});
        faces << Face __ ({{A,vec3(1,1,1),N},{C,vec3(1,1,1),N},{D,vec3(1,1,1),N}});
        // Update light bounds (Z far)
        { vec3 lightP = (sun*A).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }
        { vec3 lightP = (sun*B).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }
        { vec3 lightP = (sun*C).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }
        { vec3 lightP = (sun*D).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }

        // Compute scene bounds in world space to fit view
        for(const Face& face: faces) for(const Vertex& V : ref<Vertex>__(face.vertices, 3)) {
            worldMin=min(worldMin,V.position);
            worldMax=max(worldMax,V.position);
        }
        worldCenter = (worldMax+worldMin)/2.f; worldRadius=length(worldMax.yz()-worldMin.yz())/2.f; //FIXME: compute smallest enclosing sphere

        // Submits geometry (TODO: indexed)
        buffer.upload<Vertex>(cast<Vertex>(ref<Face>(faces)));

        window.render();
    }

    uint64 frameEnd=cpuTime(), frameTime=20000; // last frame end and initial frame time estimation in microseconds

    void render(int2 position, int2 size) override {
        uint width=size.x, height = size.y;
        // Computes projection transform
        mat4 projection;
        projection.perspective(PI/4,width,height,1,4);
        // Computes view transform
        mat4 view;
        view.scale(1.f/worldRadius); // fit scene (isometric approximation)
        view.translate(vec3(0,0,-2*worldRadius)); // step back
        view.rotateX(rotation.y); // yaw
        view.rotateY(rotation.x); // pitch
        view.rotateZ(PI/2); //+X (heading) is up
        view.translate(vec3(-worldCenter.x,0,0)); //view.translate(-worldCenter); //center origin
        // View-space lighting
        mat3 normalMatrix = view.normalMatrix();
        vec3 sunLightDirection = normalize((view*sun.inverse()).normalMatrix()*vec3(0,0,-1));
        vec3 skyLightDirection = normalize(normalMatrix*vec3(1,0,0));

        // Render sun shadow map
        sunShadow = GLFrameBuffer()
        sun=mat4();
        sun.scale(lightMax-lightMin);
        sun.translate(-lightMin);
        sun.rotateY(PI/4);
        buffer.draw();

        if(framebuffer.width != width || framebuffer.height != height) framebuffer=GLFrameBuffer(width,height);
        framebuffer.bind(true);
        glDepthTest(true);
        glCullFace(true);
        glBlend(false);

        shader["modelViewProjectionTransform"] = projection*view;
        shader["normalMatrix"] = normalMatrix;
        //shader["sunLightTransform"] = sun;
        shader["sunLightDirection"] = sunLightDirection;
        shader["skyLightDirection"] = skyLightDirection;
        buffer.bindAttribute(shader,"position",3,__builtin_offsetof(Vertex,position));
        buffer.bindAttribute(shader,"color",3,__builtin_offsetof(Vertex,color));
        buffer.bindAttribute(shader,"normal",3,__builtin_offsetof(Vertex,normal));
        buffer.draw();

        GLTexture color(width,height,GLTexture::RGB16F);
        framebuffer.blit(color);

        GLFrameBuffer::bindWindow(int2(position.x,window.size.y-height-position.y), size);
        glDepthTest(false);
        glCullFace(false);

        resolve.bindSamplers("framebuffer"); GLTexture::bindSamplers(color);
        glDrawRectangle(resolve,vec2(-1,-1),vec2(1,1));

        GLFrameBuffer::bindWindow(0, window.size);

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
