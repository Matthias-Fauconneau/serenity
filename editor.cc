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

constexpr bool UI = true;

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
    Window window __(&layout, int2(0,0), "L-System Editor"_, false);

    // Renderer
    GLFrameBuffer sunShadow;
    GLFrameBuffer framebuffer;
    GLBuffer buffer;
    SHADER(shadow) GLShader& shadow = shadowShader();
    SHADER(shader) GLShader& shader = shaderShader();
    SHADER(resolve) GLShader& resolve = resolveShader();

    // Scene
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
    vec3 worldCenter=0; float worldRadius=0; // Scene bounding sphere in world space
    vec3 lightMin=0, lightMax=0; // Scene bounding box in light space

    // Generated scene geometry data
    struct Vertex {
        vec3 position; // World-space position
        vec3 color; // BGR albedo (TODO: texture mapping)
        vec3 normal; // World-space vertex normals
    };
    array<Vertex> vertices;
    array<uint> indices;

    // Creates a new face using existing vertices when possible (normals will be smoothed with adjacent faces)
    template<uint N> void face(const Vertex (&polygon)[N]) {
        static_assert(N>=3,"");
        struct Match { float distance=1; uint index = -1;};
        Match matches[N];
        for(uint index: range(vertices.size())) { //FIXME: generating N vertices is O(N^2) in total (need object & space partition)
            Vertex& v = vertices[index];
            for(uint i: range(N)) {
                const Vertex& o = polygon[i];
                if(v.color!=o.color) continue;
                float distance = sqr(v.position-o.position);
                if(distance>matches[i].distance) continue;
                if(dot(o.normal,v.normal)<0) continue;
                matches[i].distance = distance;
                matches[i].index = index;
            }
        }
        uint indices[N];
        for(uint i: range(N)) {
            const Match& match = matches[i];
            if(match.index!=uint(-1)) {
                Vertex& v = vertices[match.index];
                v.normal += polygon[i].normal;
                indices[i] = match.index;
            } else {
                if(vertices.size()==vertices.capacity()) vertices.reserve(2*vertices.size());
                vertices << polygon[i];
                indices[i] = vertices.size()-1;
            }
        }
        uint a = indices[0];
        uint b = indices[1];
        for(uint i: range(2,N)) { // Tesselates convex polygons as fans (FIXME: generates bad triangle)
            if(this->indices.size()==this->indices.capacity()) this->indices.reserve(2*this->indices.size());
            uint c = indices[i];
            this->indices << a << b << c;
            b = c;
        }
    }

    // Creates a new flat face, normals will be smoothed with adjacent faces
    template<uint N> void face(const vec3 (&polygon)[N], vec3 color) {
        vec3 a = polygon[0];
        vec3 b = polygon[1];
        Vertex vertices[N]={{a,color,0},{b,color,0}};
        for(uint i: range(2,N)) { // Fan
            vec3 c = polygon[i];
            vec3 surface = cross(b-a,c-a);
            vertices[i-2].normal += surface;
            vertices[i-1].normal += surface;
            vertices[i] = __(c,color,surface);
            b = c;
        }
        face(vertices);
    }

     /// Setups the application UI and shortcuts
     Editor() {
         if(UI) layout << &systems << this << &editor;
         else window.widget=this;
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
        vertices.clear(); indices.clear(); worldMin=0, worldMax=0, worldCenter=0, worldRadius=0, lightMin=0, lightMax=0;

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
                    else { // Tesselated branch
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
                            face((vec3[]){a0,a1,b1,b0},color);
                        }
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
                polygon.vertices.reserve(4);
                polygon.color = color;
            } else if(symbol=="}"_) { // stores current polygon and pop previous nesting level from stack
                vec3* V = polygon.vertices.data();
                uint N = polygon.vertices.size();
                if(N<3) { userError("N<3"); return; }
                else {
                    vec3 dn = normalize(cross(V[1]-V[0],V[2]-V[0])); // avoid self-shadowing
                    if(N==3) {
                        face<3>((vec3[]){V[0]+dn,V[1]+dn,V[2]+dn}, polygon.color);
                        face<3>((vec3[]){V[2]-dn,V[1]-dn,V[0]-dn}, polygon.color); //Two-sided
                    }
                    else if(N==4) {
                        face<4>((vec3[]){V[0]+dn,V[1]+dn,V[2]+dn,V[3]+dn}, polygon.color);
                        face<4>((vec3[]){V[3]-dn,V[2]-dn,V[1]-dn,V[0]-dn}, polygon.color); //Two-sided
                    }
                    else {
                        vec3 A = polygon.vertices.first();
                        vec3 B = polygon.vertices[1];
                        for(vec3 C: polygon.vertices.slice(2)) {
                            face((vec3[]){A+dn, B+dn, C+dn}, polygon.color);
                            face((vec3[]){C-dn, B-dn, A-dn}, polygon.color); //Two-sided
                            B = C;
                        }
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
        for(Vertex& vertex: vertices) {
            vec3 P = (sun*vertex.position).xyz();
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
        face((vec3[]){A,B,C,D}, vec3(1,1,1));
        // Update light bounds (Z far)
        { vec3 lightP = (sun*A).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }
        { vec3 lightP = (sun*B).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }
        { vec3 lightP = (sun*C).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }
        { vec3 lightP = (sun*D).xyz(); lightMin=min(lightMin,lightP); lightMax=max(lightMax,lightP); }

        // Compute scene bounds in world space to fit view
        for(Vertex& vertex: vertices) {
            worldMin=min(worldMin, vertex.position);
            worldMax=max(worldMax, vertex.position);
        }
        worldCenter = (worldMax+worldMin)/2.f; worldRadius=length(worldMax.yz()-worldMin.yz())/2.f; //FIXME: compute smallest enclosing sphere


        // Normalizes smoothed normals (weighted by triangle areas)
        for(Vertex& vertex: vertices) vertex.normal=normalize(vertex.normal);

        // Submits geometry
        buffer.upload<Vertex>(vertices);
        buffer.upload(indices);

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
        if(!sunShadow) sunShadow = GLFrameBuffer(GLTexture(1024,1024,GLTexture::Depth24|GLTexture::Shadow|GLTexture::Bilinear));
        sunShadow.bind(true);
        glDepthTest(true);
        glCullFace(true);

        // Normalizes to -1,1
        sun=mat4();
        sun.translate(-1);
        sun.scale(2.f/(lightMax-lightMin));
        sun.translate(-lightMin);
        sun.rotateY(PI/4);

        shadow["modelViewProjectionTransform"] = sun;
        buffer.bindAttribute(shadow,"position",3,__builtin_offsetof(Vertex,position));
        buffer.draw();

        // Normalizes to xy to 0,1 and z to -1,1
        sun=mat4();
        sun.translate(vec3(0,0,0));
        sun.scale(vec3(1.f/(lightMax.x-lightMin.x),1.f/(lightMax.y-lightMin.y),1.f/(lightMax.z-lightMin.z)));
        sun.translate(-lightMin);
        sun.rotateY(PI/4);

        if(framebuffer.width != width || framebuffer.height != height) framebuffer=GLFrameBuffer(width,height);
        framebuffer.bind(true);
        glBlend(false);

        shader["modelViewProjectionTransform"] = projection*view;
        shader["normalMatrix"] = normalMatrix;
        shader["sunLightTransform"] = sun;
        shader["sunLightDirection"] = sunLightDirection;
        shader["skyLightDirection"] = skyLightDirection;
        shader["shadowScale"] = 1.f/sunShadow.depthTexture.width;
        shader.bindSamplers("shadowMap"); GLTexture::bindSamplers(sunShadow.depthTexture);
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
        if(UI) Text(system.parseErrors ? copy(userErrors) :
                                  userErrors ? move(userErrors) :
                                               ftoa(1e6f/frameTime,1)+"fps "_+str(frameTime/1000)+"ms "_+str(indices.size()/3)+" faces\n"_)
                .render(int2(position+int2(16)));
#if 0
        window.render(); //keep updating to get maximum performance profile
#endif
    }
} application;
