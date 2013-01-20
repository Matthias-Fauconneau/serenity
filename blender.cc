#include "blender.h"
#include "process.h"
#include "string.h"
#include "data.h"
#include "gl.h"
#include "window.h"

/// Used to fix pointers in .blend file-block sections
struct Block {
    uint64 begin; // First memory address used by pointers pointing in this block
    uint64 end; // Last memory address used by pointers pointing in this block
    int64 delta; // Target memory address where the block was loaded - begin
};
bool operator<(const Block& a, const Block& b) { return a.begin<b.begin; }

/// SDNA type definitions
struct Struct {
    ref<byte> name;
    uint size;
    struct Field {
        ref<byte> typeName;
        uint reference;
        ref<byte> name;
        uint count;
        const Struct* type;
    };
    array<Field> fields;
};
string str(const Struct::Field& f) { return " "_+f.typeName+(f.reference?"* "_:" "_)+f.name+(f.count!=1?"["_+str(f.count)+"]"_:string())+";"_; }
string str(const Struct& s) { return "struct "_+s.name+" {\n"_+str(s.fields,'\n')+"\n};"_; }

/// Parses a .blend file
struct BlendView : Widget {
    // View
    int2 lastPos; // last cursor position to compute relative mouse movements
    vec2 rotation=vec2(0,-PI/3); // current view angles (yaw,pitch)
    Window window __(this, int2(1050,1050), "BlendView"_, Window::OpenGL);

    // Renderer
    GLFrameBuffer framebuffer;
    //SHADER(shadow) GLShader& shadow = shadowShader();
    SHADER(shader) GLShader& shader = shaderShader();
    SHADER(sky) GLShader& sky = skyShader();
    SHADER(resolve) GLShader& resolve = resolveShader();

    // File
    Map map; // Keep file mmaped
    const Scene* scene=0; // Blender scene (root handle to access all data)

    // Scene
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
    vec3 worldCenter=0; float worldRadius=0; // Scene bounding sphere in world space
    /*vec3 lightMin=0, lightMax=0; // Scene bounding box in light space
    const float sunPitch = 3*PI/4;*/

    // Geometry
    struct Vertex {
        vec3 position; // World-space position
        vec3 color; // BGR albedo (TODO: texture mapping)
        vec3 normal; // World-space vertex normals
    };
    array<GLBuffer> buffers;

    BlendView()
        : map("Island/Island.blend", home(), Map::Read|Map::Write) { // with write access to fix pointers (TODO: write back fixes for faster loads)
        load();
        parse();

        window.localShortcut(Escape).connect(&::exit);
    }

    /// Recursively fix all pointers in an SDNA structure
    void fix(const array<Block>& blocks, const Struct& type, const ref<byte>& buffer) {
        BinaryData data (buffer);
        for(const Struct::Field& field: type.fields) {
            if(!field.reference) {
                if(field.type->fields) for(uint i unused: range(field.count)) fix(blocks, *field.type, data.Data::read(field.type->size));
                else data.advance(field.count*field.type->size);
            } else for(uint i unused: range(field.count)) {
                uint64& pointer = (uint64&)data.read<uint64>();
                if(!pointer) continue;
                for(const Block& block: blocks.slice(blocks.binarySearch( Block __(pointer) )-1)) {
                    if(pointer >= block.begin && pointer < block.end) {
                        pointer += block.delta;
                        goto found;
                    }
                }
                pointer = 0;
                found:;
            }
        }
    }

    /// Parses SDNA to fix all pointers
    void load() {
        array<Block> blocks; // List of file blocks containing pointers to be fixed
        array<Struct> structs; // SDNA structure definitions

        BinaryData file(map);
        //Assumes BLENDER-v262 (64bit little endian)
        file.seek(12);
        blocks.reserve(32768);
        while(file) { // Parses SDNA
            const BlockHeader& header = file.read<BlockHeader>();
            ref<byte> identifier(header.identifier,4);
            BinaryData data( file.Data::read(header.size) );
            blocks << Block __(header.address, header.address+header.size, int64(uint64(data.buffer.buffer.data)-header.address));
            if(identifier == "DNA1"_) {
                data.advance(4); //SDNA
                data.advance(4); //NAME
                uint nameCount = data.read();
                array< ref<byte> > names;
                for(uint unused i: range(nameCount)) names << data.untilNull();
                data.align(4);
                data.advance(4); //TYPE
                uint typeCount = data.read();
                array< ref<byte> > types;
                for(uint unused i: range(typeCount)) types << data.untilNull();
                data.align(4);
                data.advance(4); //TLEN
                ref<uint16> lengths = data.read<uint16>(typeCount);
                data.align(4);
                data.advance(4); //STRC
                uint structCount = data.read();
                for(uint unused i: range(structCount)) {
                    Struct s;
                    uint16 name = data.read();
                    s.size = lengths[name];
                    s.name = types[name];
                    uint16 fieldCount = data.read();
                    for(uint unused i: range(fieldCount)) {
                        uint16 type = data.read();
                        uint16 name = data.read();
                        Struct::Field f __(types[type], 0, names[name], 1, 0);
                        if(f.name[0]=='(') { //parse function pointers
                            f.name.data+=2; f.name.size-=2+3;
                            f.reference++;
                        } else {
                            while(f.name[0]=='*') { //parse references
                                f.name.data++; f.name.size--;
                                f.reference++;
                            }
                        }
                        for(uint i=0;i<f.name.size;i++) { //parse static arrays
                            if(f.name[i]=='[') {
                                f.count = toInteger(f.name.slice(i+1,f.name.size-1-(i+1)));
                                f.name.size = i;
                            }
                        }
                        s.fields << f;
                    }
                    structs << move(s);
                }
                structs << Struct __("char"_,1) << Struct __("short"_,2) << Struct __("int"_,4) << Struct __("uint64_t"_,8)
                        << Struct __("float"_,4) << Struct __("double"_,8);
                for(Struct& s: structs) {
                    for(Struct::Field& f: s.fields) {
                        for(const Struct& match: structs) if(match.name == f.typeName) { f.type = &match; break; }
                        assert(f.type || f.reference);
                    }
                }
            }
            if(identifier == "ENDB"_) break;
        }
        quicksort(blocks);

        file.seek(12);
        while(file) { // Fixes pointers
            const BlockHeader& header = file.read<BlockHeader>();
            ref<byte> identifier(header.identifier,4);
            Data data (file.Data::read(header.size));

            if(identifier == "DNA1"_) continue;
            if(identifier == "ENDB"_) break;
            if(identifier == "SC\0\0"_) scene = (Scene*)data.buffer.buffer.data;

            const Struct& type = structs[header.type];
            if(header.size < header.count*type.size) {
                //log("WARNING: header.size < header.count*type.size", identifier, header.count, type.size, header.size, type);
            } else {
                if(type.fields) for(uint unused i: range(header.count)) fix(blocks, type, data.read(type.size));
            }
        }

        //for(const Struct& match: structs) if(match.name == "MLoop"_) { log(match); break; }
    }

    /// Extracts relevant data from Blender structures
    void parse() {
        //sun=mat4(); sun.rotateX(sunPitch);

        for(const Base& base: scene->base) {
            if(base.object->type==Object::Mesh) {
                const Mesh* mesh = base.object->data;

                ref<MVert> verts (mesh->mvert, mesh->totvert);
                array<Vertex> vertices;
                for(const MVert& vert: verts) {
                    vec3 position = vec3(vert.co);
                    vec3 normal = normalize(vec3(vert.no[0],vert.no[1],vert.no[2]));
                    vec3 color = vec3(1,1,1); //TODO: MCol

                    /*// Compute scene bounds in light space to fit shadow
                    vec3 P = (sun*vertex.position).xyz();
                    lightMin=min(lightMin,P);
                    lightMax=max(lightMax,P);*/

                    // Computes scene bounds in world space to fit view
                    worldMin=min(worldMin, position);
                    worldMax=max(worldMax, position);

                    vertices << Vertex __(position, normal, color);
                }

                ref<MPoly> polys (mesh->mpoly, mesh->totpoly);
                ref<MLoop> loops (mesh->mloop, mesh->totloop);
                array<uint> indices;
                for(const MPoly& poly: polys) {
                    assert(poly.totloop==3 || poly.totloop==4);
                    uint a=loops[poly.loopstart].v, b=loops[poly.loopstart+1].v;
                    for(uint i: range(poly.loopstart+2, poly.loopstart+poly.totloop)) {
                        uint c = loops[i].v;
                        indices << a << b << c;
                        b = c;
                    }
                }

                // Submits geometry
                GLBuffer buffer;
                buffer.upload<Vertex>(vertices);
                buffer.upload(indices);
                buffers << move(buffer);

                //log(base.object->id.name, mesh->id.name, vertices.size(), indices.size());
            }
        }

        //FIXME: compute smallest enclosing sphere
        worldCenter = (worldMin+worldMax)/2.f; worldRadius=length(worldMax.xy()-worldMin.xy())/2.f;
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

    void render(int2 unused position, int2 unused size) override {
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
        /*vec3 sunLightDirection = normalize((view*sun.inverse()).normalMatrix()*vec3(0,0,-1));
        vec3 skyLightDirection = normalize(normalMatrix*vec3(0,0,1));

        // Render sun shadow map
        if(!sunShadow)
            sunShadow = GLFrameBuffer(GLTexture(4096,4096,GLTexture::Depth24|GLTexture::Shadow|GLTexture::Bilinear|GLTexture::Clamp));
        sunShadow.bind(true);*/
        glDepthTest(true);
        glCullFace(true);

        // Normalizes to -1,1
        /*sun=mat4();
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
        sun.rotateX(sunPitch);*/

        if(framebuffer.width != width || framebuffer.height != height) framebuffer=GLFrameBuffer(width,height);
        framebuffer.bind(true);
        glBlend(false);

        shader.bind();
        shader["modelViewProjectionTransform"] = projection*view;
        shader["normalMatrix"] = normalMatrix;
        /*shader["sunLightTransform"] = sun;
        shader["sunLightDirection"] = sunLightDirection;
        shader["skyLightDirection"] = skyLightDirection;
        shader["shadowScale"] = 1.f/sunShadow.depthTexture.width;
        shader.bindSamplers("shadowMap"); GLTexture::bindSamplers(sunShadow.depthTexture);*/
        for(GLBuffer& buffer: buffers) {
            buffer.bindAttribute(shader,"position",3,__builtin_offsetof(Vertex,position));
            buffer.bindAttribute(shader,"color",3,__builtin_offsetof(Vertex,color));
            buffer.bindAttribute(shader,"normal",3,__builtin_offsetof(Vertex,normal));
            buffer.draw();
        }

        //TODO: fog
        sky["inverseProjectionMatrix"] = projection.inverse();
        //sky["sunLightDirection"] = -sunLightDirection;
        glDrawRectangle(sky,vec2(-1,-1),vec2(1,1));

        GLTexture color(width,height,GLTexture::RGB16F);
        framebuffer.blit(color);

        GLFrameBuffer::bindWindow(int2(position.x,window.size.y-height-position.y), size);
        glDepthTest(false);
        glCullFace(false);

        resolve.bindSamplers("framebuffer"); GLTexture::bindSamplers(color);
        glDrawRectangle(resolve,vec2(-1,-1),vec2(1,1));

        GLFrameBuffer::bindWindow(0, window.size);
    }

} application;
