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
    vec2 rotation=vec2(0,-PI/3); // current view angles (yaw,pitch)
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
    SHADER(sky) GLShader& sky = skyShader();
    SHADER(resolve) GLShader& resolve = resolveShader();

    // Scene
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
    vec3 worldCenter=0; float worldRadius=0; // Scene bounding sphere in world space
    vec3 lightMin=0, lightMax=0; // Scene bounding box in light space
    const float sunPitch = 3*PI/4;

    // Generated scene geometry data
    struct Vertex {
        vec3 position; // World-space position
        vec3 color; // BGR albedo (TODO: texture mapping)
        vec3 normal; // World-space vertex normals
    };
    array<Vertex> vertices;
    array<uint> indices;
    static constexpr uint G = 16; // Grid resolution (TODO: adapt with vertex count)
    vec3 gridMin=-1, gridMax=1; // current grid bounds
    array<uint> grid[G*G*G]; //Space partionning for faster vertex index lookups
    array<uint>& cell(vec3 p) {
        assert(p>gridMin && p<gridMax, gridMin, p, gridMax);
        vec3 n = float(G)*(p-gridMin)/(gridMax-gridMin);
        uint i = (uint(n.z)*G+uint(n.y))*G+uint(n.x);
        if(i>=G*G*G) i=G*G*G-1; //assert(i<G*G*G);
        return grid[i];
    }

    // Creates a new face using existing vertices when possible (normals will be smoothed with adjacent faces)
    template<uint N> void face(const Vertex (&polygon)[N]) {
        static_assert(N>=3,"");
        uint indices[N];
        for(uint i: range(N)) { // Lookups each vertex
            const Vertex& o = polygon[i];

            if(!(o.position>gridMin && o.position<gridMax)) {
                gridMin=min(gridMin,o.position-vec3(1)); gridMax=max(gridMax,o.position+vec3(1)); //Resizes grid
                for(array<uint>& cell: grid) cell.clear(); // Clears grid
                for(uint i: range(vertices.size())) cell(vertices[i].position) << i; // Sorts all vertices
            }

            float minDistance=1; uint minIndex = -1;
            for(uint index: cell(o.position)) {
                Vertex& v = vertices[index];

                if(v.color!=o.color) continue;
                float distance = sqr(v.position-o.position);
                if(distance>minDistance) continue;
                if(dot(o.normal,v.normal)<0) continue;
                minDistance = distance;
                minIndex = index;
            }
            if(minIndex!=uint(-1)) {
                Vertex& v = vertices[minIndex];
                v.normal += o.normal;
                indices[i] = minIndex;
            } else {
                if(vertices.size()==vertices.capacity()) vertices.reserve(2*vertices.size());
                uint index = vertices.size();
                vertices << o;
                cell(o.position) << index;
                indices[i] = index;
            }
        }
        // Appends polygon indices
        uint a = indices[0];
        uint b = indices[1];
        for(uint i: range(2,N)) { // Tesselates convex polygons as fans (FIXME: generates bad triangle)
            if(this->indices.size()==this->indices.capacity()) this->indices.reserve(2*this->indices.size());
            uint c = indices[i];
            this->indices << a << b << c;
            b = c;
        }
        //TODO: triangle strips
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

    /*vec2 randomNormals[256*256];
    // Returns a random 2D normal from position
    vec2 random2D(int2 i) { return randomNormals[(i.y%256)*256+(i.x%256)]; }

    float perlin2D( vec2 p ) {
      int2 i = int2(p);
      vec2 f = p-floor(p);
      float v00 = dot(random2D(i+int2(0,0)), f - vec2(0,0));
      float v01 = dot(random2D(i+int2(0,1)), f - vec2(0,1));
      float v10 = dot(random2D(i+int2(1,0)), f - vec2(1,0));
      float v11 = dot(random2D(i+int2(1,1)), f - vec2(1,1));
      vec2 w = f*f*f*(f*(6.f*f-vec2(15.f))+vec2(10.f));
      return mix( mix( v00, v01, w.y ), mix( v10, v11, w.y ), w.x );
    }*/

    float bilinear(float* image, uint stride, vec2 uv) {
        uv *= stride;
        int2 i = int2(uv);
        vec2 f = uv-floor(uv);
        return mix(
                    mix( image[(i.y+0)*(stride+1)+i.x], image[(i.y+0)*(stride+1)+i.x+1], f.x),
                mix( image[(i.y+1)*(stride+1)+i.x], image[(i.y+1)*(stride+1)+i.x+1], f.x), f.y);
    };

     /// Setups the application UI and shortcuts
     Editor() {
         //Random random; for(vec2& v: randomNormals) v=normalize(vec2(random(),random()));

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
             rotation.y= clip(float(-PI/2),rotation.y,float(0)); // Keep pitch between [-PI/2,0]
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
        gridMin=-1, gridMax=1; for(array<uint>& cell: grid) cell.clear(); // Clear grid

        const vec3 groundColor(2./8,1./8,0);
        const vec3 grassColor(2./4,4./4,0);

        float groundSize=4000;

        Random random;
        const int N=4;
        float altitude[(N+1)*(N+1)];
        for(int y=0;y<=N;y++) for(int x=0;x<=N;x++) {
            altitude[y*(N+1)+x] = 100.f*random();
        }

        {// Terrain
            for(int y=0;y<N;y++) for(int x=0;x<N;x++) {
                vec2 min = vec2((x-N/2)*groundSize/N,(y-N/2)*groundSize/N);
                vec2 max = vec2((x-N/2+1)*groundSize/N,(y-N/2+1)*groundSize/N);
                face((vec3[]){ //FIXME: triangular mesh
                         vec3(min.x,min.y,altitude[(y+0)*(N+1)+(x+0)]),
                         vec3(max.x,min.y,altitude[(y+0)*(N+1)+(x+1)]),
                         vec3(max.x,max.y,altitude[(y+1)*(N+1)+(x+1)]),
                         vec3(min.x,max.y,altitude[(y+1)*(N+1)+(x+0)])}, groundColor);
            }
        }

        {// Grass (TODO: speed up generation, parsed definition)
            const int bladeCountSqrt=256;
            vertices.reserve(vertices.size()+bladeCountSqrt*bladeCountSqrt*8);
            indices.reserve(indices.size()+bladeCountSqrt*bladeCountSqrt*8*3);

            for(int y=0;y<bladeCountSqrt;y++) for(int x=0;x<bladeCountSqrt;x++) {
                float bent = 1./4*(1./2+(random()+1)/2);
                float angle = random()*PI;
                float width = 6*(1./2+(random()+1)/2);
                float height = 30*(1./4+(random()+1)/2);
                float gravity = 1./4;

                vec2 uv = vec2(x + (random()+1)/2, y + (random()+1)/2)/float(bladeCountSqrt);
                vec3 position = vec3(groundSize * (uv-vec2(1/2.f)), bilinear(altitude, N, uv));
                vec3 velocity = vec3(bent*cos(angle),bent*sin(angle),1);
                vec3 tangent = vec3(-sin(angle),cos(angle),0);
                vec3 blade[8];
                blade[0] = position - width*tangent;
                for(int i=1;i<8;i++) {
                    blade[i] = position + (i%2?1:-1)*width*(7-i)/7.f*tangent;
                    position += height*velocity;
                    velocity.z -= gravity;
                    if(i>=2) {
                        face((vec3[]){blade[i-2],blade[i-1],blade[i]}, grassColor);
                        face((vec3[]){blade[i],blade[i-1],blade[i-2]}, grassColor); // Two sided
                    }
                }
            }
        }

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
        stateB.translate(vec3(0,0,altitude[(N/2)*(N+1)+(N/2)]));
        state=stateB;
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
                state.translate(vec3(0,0,l)); //forward axis is +X
                if(symbol=="F"_) { //Apply tropism
                    if(system.constants.contains(string("tropism"_))) {
                        float tropism = system.constants.at(string("tropism"_));
                        vec3 Z = state[2].xyz();
                        vec3 Y = cross(vec3(0,0,-1),Z);
                        float y = length(Y);
                        if(y>0.01) { //only if Z is not colinear with tropism vector
                            assert(Y.z==0);
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
                            vec3 a0 = (A * (vec3(cos(ta0), sin(ta0), 0))).xyz();
                            vec3 a1 = (A * (vec3(cos(ta1), sin(ta1), 0))).xyz();
                            int bestJ=-1; float bestD=__FLT_MAX__;
                            for(int j=0;j<Na;j++) { //FIXME: find out the smarter way to solve this
                                float tb0 = 2*PI*j/Nb, tb1 = 2*PI*(j+1)/Nb;
                                vec3 b0 = (B * (vec3(cos(tb0), sin(tb0), 0))).xyz();
                                vec3 b1 = (B * (vec3(cos(tb1), sin(tb1), 0))).xyz();
                                vec3 a = A[3].xyz(), b = B[3].xyz();
                                float d = length((a0-a)-(b0-b))+length((a1-a)-(b1-b));
                                if(d<bestD) bestJ=j, bestD=d;
                            }
                            float tb0 = 2*PI*bestJ/Nb, tb1 = 2*PI*(bestJ+1)/Nb;
                            vec3 b0 = (B * (vec3(cos(tb0), sin(tb0), 0))).xyz();
                            vec3 b1 = (B * (vec3(cos(tb1), sin(tb1), 0))).xyz();
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
                            vec3 Z = state[2].xyz();
                            vec3 Y = cross(vec3(0,0,1),Z);
                            float y = length(Y);
                            if(y<0.01) continue; //X is colinear to vertical (all possible Y are already horizontal)
                            Y /= y;
                            assert(Y.x==0);
                            vec3 X = cross(Z,Y);
                            state[0] = vec4(X,0.f);
                            state[1] = vec4(Y,0.f);
            } else if(symbol=="\\"_||symbol=="/"_||symbol=="&"_||symbol=="^"_||symbol=="-"_ ||symbol=="+"_) {
                float a = module.arguments ? module.arguments[0]*PI/180 : system.constants.value(string("angle"_),90)*PI/180;
                if(symbol=="\\"_||symbol=="/"_) state.rotateZ(symbol=="\\"_?a:-a);
                else if(symbol=="&"_||symbol=="^"_) state.rotateY(symbol=="&"_?a:-a);
                else if(symbol=="-"_ ||symbol=="+"_) state.rotateX(symbol=="+"_?a:-a);
            } else if(symbol=="|"_) state.rotateZ(PI);
        }

        sun=mat4(); sun.rotateX(sunPitch);
        for(Vertex& vertex: vertices) {
            // Normalizes smoothed normals (weighted by triangle areas)
            vertex.normal=normalize(vertex.normal);

            // Compute scene bounds in light space to fit shadow
            vec3 P = (sun*vertex.position).xyz();
            lightMin=min(lightMin,P);
            lightMax=max(lightMax,P);

            // Computes scene bounds in world space to fit view
            worldMin=min(worldMin, vertex.position);
            worldMax=max(worldMax, vertex.position);
        }
        worldCenter = (worldMin+worldMax)/2.f; worldRadius=length(worldMax.xy()-worldMin.xy())/2.f; //FIXME: compute smallest enclosing sphere

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
        projection.perspective(PI/4,width,height,1./4,4);
        // Computes view transform
        mat4 view;
        view.scale(1.f/worldRadius); // fit scene (isometric approximation)
        view.translate(vec3(0,0,-1*worldRadius)); // step back
        view.rotateX(rotation.y); // yaw
        view.rotateZ(rotation.x); // pitch
        view.translate(vec3(0,0,-worldCenter.z));
        // View-space lighting
        mat3 normalMatrix = view.normalMatrix();
        vec3 sunLightDirection = normalize((view*sun.inverse()).normalMatrix()*vec3(0,0,-1));
        vec3 skyLightDirection = normalize(normalMatrix*vec3(0,0,1));

        // Render sun shadow map
        if(!sunShadow)
            sunShadow = GLFrameBuffer(GLTexture(4096,4096,GLTexture::Depth24|GLTexture::Shadow|GLTexture::Bilinear|GLTexture::Clamp));
        sunShadow.bind(true);
        glDepthTest(true);
        glCullFace(true);

        // Normalizes to -1,1
        sun=mat4();
        sun.translate(-1);
        sun.scale(2.f/(lightMax-lightMin));
        sun.translate(-lightMin);
        sun.rotateX(sunPitch);

        shadow["modelViewProjectionTransform"] = sun;
        buffer.bindAttribute(shadow,"position",3,__builtin_offsetof(Vertex,position));
        buffer.draw();

        // Normalizes to xy to 0,1 and z to -1,1
        sun=mat4();
        sun.translate(vec3(0,0,0));
        sun.scale(vec3(1.f/(lightMax.x-lightMin.x),1.f/(lightMax.y-lightMin.y),1.f/(lightMax.z-lightMin.z)));
        sun.translate(-lightMin);
        sun.rotateX(sunPitch);

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

        //TODO: fog
        sky["inverseProjectionMatrix"] = projection.inverse();
        sky["sunLightDirection"] = -sunLightDirection;
        glDrawRectangle(sky,vec2(-1,-1),vec2(1,1));

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
                                               ftoa(1e6f/frameTime,1)+"fps "_+str(frameTime/1000)+"ms "_+str(indices.size()/3)+" faces\n"_)
                .render(int2(position+int2(16)));
#if 0
        window.render(); //keep updating to get maximum performance profile
#endif
    }
} application;
