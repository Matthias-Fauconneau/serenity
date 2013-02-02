#include "blender.h"
#include "process.h"
#include "string.h"
#include "data.h"
#include "map.h"
#include "time.h"
#include "jpeg.h"
#include "window.h"
#include "gl.h"
SHADER(blender)

// Quicksort
template<class T> uint partition(array<T>& at, uint left, uint right, uint pivotIndex) {
    swap(at[pivotIndex], at[right]);
    const T& pivot = at[right];
    uint storeIndex = left;
    for(uint i: range(left,right)) {
        if(at[i] < pivot) {
            swap(at[i], at[storeIndex]);
            storeIndex++;
        }
    }
    swap(at[storeIndex], at[right]);
    return storeIndex;
}
template<class T> void quicksort(array<T>& at, uint left, uint right) {
    if(left < right) { // If the list has 2 or more items
        uint pivotIndex = partition(at, left, right, (left + right)/2);
        if(pivotIndex) quicksort(at, left, pivotIndex-1);
        quicksort(at, pivotIndex+1, right);
    }
}
/// Quicksorts the array in-place
template<class T> void quicksort(array<T>& at) { quicksort(at, 0, at.size()-1); }

/// Used to fix pointers in .blend file-block sections
struct Block {
    uint64 begin; // First memory address used by pointers pointing in this block
    uint64 end; // Last memory address used by pointers pointing in this block
    int64 delta; // Target memory address where the block was loaded - begin
};
bool operator<(const Block& a, const Block& b) { return a.begin<b.begin; }
string str(const Block& b) { return str(b.begin)+str("-")+str(b.end); }

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
string str(const Struct::Field& f) { return " "_+f.typeName+repeat('*',f.reference)+" "_+f.name+(f.count!=1?"["_+str(f.count)+"]"_:string())+";"_; }
string str(const Struct& s) { return "struct "_+s.name+" {\n"_+str(s.fields,'\n')+"\n};"_; }

// Renderer definitions
struct Vertex {
    vec3 position; // World-space position
    vec3 normal; // World-space vertex normals
    vec2 texCoord; // Texture coordinates
    bool operator==(const Vertex& o){
        const float e = 0x1.0p-16f;
        return sqr(position-o.position)<e && sqr(normal-o.normal)<e && sqr(texCoord-o.texCoord)<e;
    }
};
string str(const Vertex& v) { return "("_+str(v.position, v.normal, v.texCoord)+")"_; }


/// Parses a .blend file
struct BlendView : Widget {
    // View
    int2 lastPos; // last cursor position to compute relative mouse movements
    vec2 rotation=vec2(PI/3,-PI/3); // current view angles (yaw,pitch)
    float focalLength = 90; // current focal length (in mm for a 36mm sensor)
    const float zoomSpeed = 10; // in mm/click
    Window window __(this, int2(1050,590), "BlendView"_, Window::OpenGL);

    // Renderer
    GLFrameBuffer framebuffer;
    GLShader present = GLShader(blender,"screen present"_);
    GLShader shadow = GLShader(blender,"transform"_);
    GLShader impostor = GLShader(blender,"screen impostor"_);

    // File
    Folder folder;
    Map mmap; // Keep file mmaped
    const Scene* scene=0; // Blender scene (root handle to access all data)
    string shaderSource; // Custom GLSL shader

    // Scene
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
    vec3 worldCenter=0; float worldRadius=0; // Scene bounding sphere in world space

    // Light
    vec3 lightMin=0, lightMax=0; // Scene bounding box in light space
    const float sunYaw = 4*PI/3;
    const float sunPitch = -PI/3;
    mat4 sun; // sun light transform
    GLFrameBuffer sunShadow;

    // Sky
    GLShader sky = GLShader(blender,"skymap"_);
    GLTexture skymap;

    // Geometry
    struct Model {
        mat4 transform;

        array<Vertex> vertices;

        struct Material {
            string name;
            GLShader shader;
            array<GLTexture> textures;

            GLVertexBuffer vertexBuffer;

            array<uint> indices;
            GLIndexBuffer indexBuffer;
        };
        array<Material> materials;

        struct Instance {
            mat4 transform;
            GLFrameBuffer impostor;
        };
        array<Instance> instances;

        // Rendering order (cheap to expensive, front to back)
        bool operator <(const Model& o) const { return instances.size()<o.instances.size(); }
    };
    array<Model> models;

    BlendView() : folder("Island"_,home()), mmap("Island.blend.orig", folder, Map::Read|Map::Write) { // with write access to fix pointers
        load();
        shaderSource << readFile("gpu_shader_material.glsl"_, folder) << readFile("Island.glsl", folder);
        parse();

        skymap = GLTexture(decodeImage(readFile(string("textures/"_+sky.sampler2D.first()+".jpg"_), folder)), Bilinear);

        window.clearBackground = false;
        window.localShortcut(Escape).connect(&::exit);

        glDepthTest(true);
        glCullFace(true);
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
                        if(field.reference>1) { // pointer arrays
                            assert(pointer==block.begin+block.delta);
                            assert((block.end-block.begin)%8==0);
                            uint64* array = (uint64*)pointer;
                            uint size = (block.end-block.begin)/8;
                            for(uint i : range(size)) {
                                uint64& pointer = array[i];
                                if(!pointer) continue;
                                for(const Block& block: blocks.slice(blocks.binarySearch( Block __(pointer) )-1)) {
                                    if(pointer >= block.begin && pointer < block.end) {
                                        pointer += block.delta;
                                        goto found2;
                                    }
                                }
                                error("not found");
                                pointer = 0;
                                found2:;
                            }
                        }
                        goto found;
                    }
                }
                pointer = 0; //-1
                found:;
            }
        }
    }

    /// Parses SDNA to fix all pointers
    void load() {
        array<Block> blocks; // List of file blocks containing pointers to be fixed
        array<Struct> structs; // SDNA structure definitions

        BinaryData file(mmap);
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
                    uint16 index = data.read();
                    ref<byte> name = types[index];
                    uint size = lengths[index];
                    uint16 fieldCount = data.read();
                    array<Struct::Field> fields;
                    for(uint unused i: range(fieldCount)) {
                        ref<byte> type = types[(uint16)data.read()];
                        ref<byte> name = names[(uint16)data.read()];
                        uint reference=0, count=1;
                        TextData s (name);
                        if(s.match("(*"_)) { //parse function pointers
                            name.data+=2; name.size-=2+3;
                            type = "void"_; reference++;
                        } else {
                            while(s.match('*')) { //parse references
                                name.data++; name.size--;
                                reference++;
                            }
                        }
                        s.whileNot('['); if(s.match('[')) { //parse static arrays
                            name.size -= 1+(s.buffer.size()-s.index);
                            count = s.integer();
                            s.match(']');
                            while(s.match('[')) { // Flatten multiple indices
                                count *= s.integer();
                                s.match(']');
                            }
                        }
                        fields << Struct::Field __(type, reference, name, count, 0);
                    }
                    structs << Struct __(name, size, move(fields));
                }
                structs << Struct __("char"_,1) << Struct __("short"_,2) << Struct __("int"_,4) << Struct __("uint64_t"_,8)
                        << Struct __("float"_,4) << Struct __("double"_,8);
                for(Struct& s: structs) {
                    for(Struct::Field& f: s.fields) {
                        for(const Struct& match: structs) if(match.name == f.typeName) { f.type = &match; break; }
                        assert(f.type || f.reference, f);
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
            BinaryData data (file.Data::read(header.size));
            const Struct& type = structs[header.type];

            if(identifier == "SC\0\0"_) scene = (Scene*)data.buffer.buffer.data;
            else if(identifier == "DNA1"_ ) continue;
            else if(identifier == "ENDB"_) break;

            if(header.size >= header.count*type.size && type.fields && header.type != 0) {
                if(header.size != header.count*type.size) log(identifier, header.size,header.count, type.name, type.size);
                for(uint unused i: range(header.count)) fix(blocks, type, data.Data::read(type.size));
            }
        }

        //for(const Struct& match: structs) if(match.name == ""_) log(match);
    }

    /// Extracts relevant data from Blender structures
    void parse() {
        sun=mat4(); sun.rotateX(sunPitch); sun.rotateZ(sunYaw);

        // Parses objects
        map<const Object*, uint> modelIndex;
        for(const Base& base: scene->base) {
            const Object& object = *base.object;
            Model model;

            if(object.type !=Object::Mesh) continue;
            const Mesh& mesh = *object.data;
            if(!mesh.mvert) continue;

            mat4 transform;
            for(uint i: range(4)) for(uint j: range(4)) transform(i,j) = object.obmat[j*4+i];
            float scale = length(vec3(transform(0,0),transform(1,1),transform(2,2)));
            if(scale>10) continue; // Currently ignoring water plane, TODO: water reflection

            //assert(mesh.totvert<0xFFFF, mesh.id.name, mesh.totvert);
            ref<MVert> verts(mesh.mvert, mesh.totvert);
            for(uint i: range(verts.size)) {
                const MVert& vert = verts[i];
                vec3 position = vec3(vert.co);
                vec3 normal = normalize(vec3(vert.no[0],vert.no[1],vert.no[2]));

                model.vertices << Vertex __(position, normal, vec2(0,0));
                i++;
            }

            vec3 objectMin=0, objectMax=0;
            for(Vertex& vertex: model.vertices) {
                // Computes object bounds
                objectMin=min(objectMin, vertex.position);
                objectMax=max(objectMax, vertex.position);

                if(scale<10) { // Ignores water plane
                    // Computes scene bounds in world space to fit view
                    vec3 position = (transform*vertex.position).xyz();
                    worldMin=min(worldMin, position);
                    worldMax=max(worldMax, position);

                    // Compute scene bounds in light space to fit shadow
                    vec3 P = (sun*position).xyz();
                    lightMin=min(lightMin,P);
                    lightMax=max(lightMax,P);
                }
            }
            // Scales positions and transforms to keep unit bounding box in object space
            for(Vertex& vertex: model.vertices) {
                vertex.position = (vertex.position-objectMin)/(objectMax-objectMin);
            }
            transform.translate(objectMin);
            transform.scale(objectMax-objectMin);
            model.transform = transform; //for emitter and particles

            for(int materialIndex: range(mesh.totcol)) {
                Model::Material material;
                material.name = replace(replace(simplify(toLower(str((const char*)mesh.mat[materialIndex]->id.name+2)))," "_,"_"),"."_,"_"_);

                ref<MLoop> indices(mesh.mloop, mesh.totloop);
                for(const MPoly& poly: ref<MPoly>(mesh.mpoly, mesh.totpoly)) {
                    assert(poly.totloop==3 || poly.totloop==4);
                    if(poly.mat_nr!=materialIndex) continue;
                    uint a=indices[poly.loopstart].v, b=indices[poly.loopstart+1].v;
                    for(uint index: range(poly.loopstart+2, poly.loopstart+poly.totloop)) {
                        uint c = indices[index].v;
                        material.indices << a << b << c;
                        b = c;
                    }
                }
                assert(material.indices, "Empty material");

                if(mesh.mloopuv) { // Assigns UV coordinates to vertices
                    ref<MLoopUV> texCoords(mesh.mloopuv, mesh.totloop);
                    uint i=0;
                    for(const MPoly& poly: ref<MPoly>(mesh.mpoly, mesh.totpoly)) {
                        if(poly.mat_nr!=materialIndex) continue;
                        {
                            vec2 texCoord = fract(vec2(texCoords[poly.loopstart].uv));
                            vec2& a = model.vertices[material.indices[i]].texCoord;
                            if(a && sqr(a-texCoord)>0.01) error("TODO: duplicate vertex");
                            a = texCoord;
                        }{
                            vec2 texCoord = fract(vec2(texCoords[poly.loopstart+1].uv));
                            vec2& b = model.vertices[material.indices[i+1]].texCoord;
                            if(b && sqr(b-texCoord)>0.01) error("TODO: duplicate vertex");
                            b = texCoord;
                        }
                        for(uint index: range(poly.loopstart+2, poly.loopstart+poly.totloop)) {
                            vec2 texCoord = fract(vec2(texCoords[index].uv));
                            vec2& c = model.vertices[material.indices[i+2]].texCoord;
                            if(c && sqr(c-texCoord)>0.01) error("TODO: duplicate vertex");
                            c = texCoord;
                            i += 3; // Keep indices counter synchronized with MPoly
                        }
                    }
                }

                model.materials << move(material);
            }

            bool render = scene->lay&object.lay; // Visible layers
            for(const ParticleSystem& particle: object.particlesystem) if(!(particle.part->draw&ParticleSettings::PART_DRAW_EMITTER)) render=false;
            if(render) model.instances << Model::Instance __( transform ); // Appends an instance
            modelIndex.insert(&object, models.size()); // Keep track of Object <-> Model mapping (e.g to instanciate particles)
            models << move(model);
        }

        // Parses particle systems
        for(const Base& base: scene->base) {
            for(const ParticleSystem& particle: base.object->particlesystem) {
                const Model& model = models[modelIndex.at(base.object)];
                const ParticleSettings& p = *particle.part;
                if(p.brownfac>10) continue; // FIXME: render birds with brownian dispersion
                //log("count",particle.totpart*(p.childtype?p.ren_child_nbr:1));

                // Selects N random faces
                Random random;
                for(int n=0; n</*particle.totpart/200*//*256*/16;) {
                    assert(model.materials.size()==1);
                    const array<uint>& indices = model.materials[0].indices;
                    uint index = random % (indices.size()/3) * 3;
                    uint a=indices[index], b=indices[index+1], c=indices[index+2];
                    uint densityVG = particle.vgroup[ParticleSystem::PSYS_VG_DENSITY];
                    if(densityVG) {
                        if(base.object->data->dvert[a].dw[densityVG-1].weight < 0.5) continue;
                        if(base.object->data->dvert[b].dw[densityVG-1].weight < 0.5) continue;
                        if(base.object->data->dvert[c].dw[densityVG-1].weight < 0.5) continue;
                    }

                    mat4 transform;
                    // Translates
                    vec3 A = (model.transform*model.vertices[a].position).xyz(),
                            B = (model.transform*model.vertices[b].position).xyz(),
                            C = (model.transform*model.vertices[c].position).xyz();
                    float wA = random(), wB = random(), wC = 3-wA-wB;
                    vec3 position = (wA*A+wB*B+wC*C)/3.f;
                    transform.translate(position);
                    // Scales
                    transform.scale(p.size);
                    transform.scale( 1 - p.randsize*random() ); //p.size is maximum not average
                    transform.scale(10); // HACK FIXME
                    // Orient X axis along normal
                    vec3 e2 = normalize(B-A), e1 = normalize(cross(e2,C-A)), e3 = cross(e1,e2);
                    mat4 align(mat3(e1,e2,e3));
                    transform = transform * align;
                    // Random rotation around X axis
                    transform.rotateX(2*PI*random());

                    Object* object=0;
                    if(p.dup_ob) object = p.dup_ob;
                    else {
                        uint total = 0;
                        for(const ParticleDupliWeight& p: p.dupliweights) total += p.count;
                        uint index = random % total;
                        uint i = 0;
                        for(const ParticleDupliWeight& p: p.dupliweights) {
                            if(index >= i && index<i+p.count) { object = p.ob; break; }
                            i+=p.count;
                        }
                    }
                    assert(object);

                    Model& dupli = models.at(modelIndex.at(object));
                    dupli.instances << Model::Instance __( transform*dupli.transform );
                    n++;
                }
            }
        }

        for(uint i=0; i<models.size();) {
            Model& model = models[i];
            if(!model.materials || !model.instances) { models.removeAt(i); continue; } else i++;

            for(Model::Material& material : model.materials) {
                // Remap indices
                array<uint> remap(model.vertices.size()); remap.setSize(remap.capacity()); for(uint& index: remap) index=-1;
                array<uint> indices (material.indices.size());
                array<Vertex> vertices (model.vertices.size());
                for(uint index: material.indices) {
                    uint newIndex = remap[index];
                    if(newIndex == uint(-1)) { newIndex=remap[index]=vertices.size(); vertices << model.vertices[index]; }
                    indices << newIndex;
                }

#if 0
                uint cache[16]; for(uint& index: cache) index=-1; uint fifo=0;
                uint hit=0;
                for(uint index: indices) {
                    for(uint cached: cache) if(index==cached) {hit++; break;}
                    cache[(fifo++)%16] = index;
                };
                if((float)hit/material.indices.size() < 0.617) error("TODO: optimize indices for vertex cache");
#endif
#if 0
                array<uint> tristrip (indices.size());
                uint A=indices[i+2], B=indices[i+1];
                tristrip << indices[i] << B << A;
                for(uint i=3; i<indices.size(); i+=3) {
                    uint a = indices[i], b = indices[i+1], c = indices[i+2];
                    if(a!=A || B!=b) tristrip << (vertices.size()<0x10000?0xFFFF:0xFFFFFFFF) << a << b;
                    tristrip << c;
                    A=c, B=b;
                };
                if(tristrip.size()<indices.size()) {
                    material.indexBuffer.primitiveType = TriangleStrip;
                    material.indexBuffer.primitiveRestart = true;
                    indices = move(tristrip);
                }
#endif

                // Uploads geometry
                material.vertexBuffer.upload<Vertex>(vertices);
                if(vertices.size()<=0x10000) {
                    array<uint16> indices16 (indices.size()); //16bit indices
                    for(uint index: indices) { assert(index<0x10000); indices16 << index; }
                    material.indexBuffer.upload(indices16);
                } else {
                    material.indexBuffer.upload(indices);
                }

                // Compiles shader
                material.shader =
                        GLShader(string(blender+shaderSource), string("transform normal texCoord diffuse shadow sun sky "_ +material.name));

                // Loads textures
                int unit=1;
                for(const string& name: material.shader.sampler2D) {
                    material.shader[name] = unit++;
                    material.textures<<GLTexture(decodeImage(readFile(string("textures/"_+name+".jpg"_), folder)), Mipmap|Bilinear|Anisotropic);
                }
            }
        }

        quicksort(models); // Sorts by rendering cost (i.e render most instanced object last)

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
        else if(event == Press && button == WheelDown) focalLength += zoomSpeed;
        else if(event == Press && button == WheelUp) focalLength = max(1.f,focalLength-zoomSpeed);
        else return false;
        return true;
    }

#define profile( statements ... ) statements
    profile( map<string, GLTimerQuery> lastProfile; )
    void render(int2 unused position, int2 unused size) override {
        // Render sun shadow map
        if(!sunShadow) {
            sunShadow = GLFrameBuffer(GLTexture(4096,4096,Depth24|Shadow|Bilinear|Clamp));
            sunShadow.bind(ClearDepth);

            // Normalizes to [-1,1]
            sun=mat4();
            sun.scale(vec3(1,1,-1));
            sun.translate(-1);
            sun.scale(2.f/(lightMax-lightMin));
            sun.translate(-lightMin);
            sun.rotateX(sunPitch);
            sun.rotateZ(sunYaw);

            shadow.bind();
            for(Model& model: models) {
                for(const Model::Material& material: model.materials) {
                    material.vertexBuffer.bindAttribute(shadow, "aPosition", 3, __builtin_offsetof(Vertex,position));
                    for(const Model::Instance& instance: model.instances) {
                        shadow["modelViewTransform"] = sun*instance.transform;
                        material.indexBuffer.draw();
                    }
                }
            }

            // Normalizes xyz to [0,1]
            mat4 sampler2D;
            sampler2D.scale(1./2);
            sampler2D.translate(1);
            sun = sampler2D * sun;
        }

        uint width=size.x, height = size.y;
#if 1
        if(framebuffer.width != width || framebuffer.height != height) framebuffer=GLFrameBuffer(width,height,RGB16F,-1);
        framebuffer.bind(ClearDepth);
#else
        //FIXME: sRGB framebuffer doesn't work
        GLFrameBuffer::bindWindow(int2(position.x,window.size.y-height-position.y), size, ClearDepth);
#endif

        // Computes view transform
        mat4 view;
        view.perspective(2*atan(36/(2*focalLength)), width, height, 1./4, 4);
        view.scale(1.f/worldRadius); // fit scene (isometric approximation)
        view.translate(vec3(0,0,-worldRadius)); // step back
        view.rotateX(rotation.y); // pitch
        view.rotateZ(rotation.x); // yaw
        view.translate(vec3(0,0,-worldCenter.z));

        // World-space lighting
        vec3 sunLightDirection = normalize(sun.inverse().normalMatrix()*vec3(0,0,-1));
        vec3 skyLightDirection = vec3(0,0,1);

        profile( map<string, GLTimerQuery> profile; )
        for(Model& model: models) {
            for(Model::Material& material: model.materials) {
                GLShader& shader = material.shader;
                shader.bind();
                shader["shadowScale"] = 1.f/sunShadow.depthTexture.width;
                shader["shadowMap"] = 0; sunShadow.depthTexture.bind(0);
                shader["sunLightDirection"] = sunLightDirection;
                shader["skyLightDirection"] = skyLightDirection;

                {int i=1; for(const string& name: shader.sampler2D) shader[name] = i++; }
                {int i=1; for(const GLTexture& texture : material.textures) texture.bind(i++); }

                material.vertexBuffer.bindAttribute(shader,"aPosition",3,__builtin_offsetof(Vertex,position));
                material.vertexBuffer.bindAttribute(shader,"aNormal",3,__builtin_offsetof(Vertex,normal));
                if(shader.sampler2D) material.vertexBuffer.bindAttribute(shader,"aTexCoord",2,__builtin_offsetof(Vertex,texCoord));

                profile( GLTimerQuery timerQuery; timerQuery.start(); )
                for(Model::Instance& instance : model.instances) {
                    if(!instance.impostor) {
                        instance.impostor = GLFrameBuffer(GLTexture(width/2,height/2));
                        instance.impostor.bind(ClearDepth);
                        //TODO: project bounding box
                        shader["modelViewTransform"] = view*instance.transform;
                        shader["normalMatrix"] = instance.transform.normalMatrix();
                        shader["shadowTransform"] = sun*instance.transform;
                        if(shader["modelTransform"]) shader["modelTransform"] = instance.transform;
                        material.indexBuffer.draw();
                    }
                }
                framebuffer.bind();
                for(Model::Instance& instance : model.instances) {
                    //TODO: project bounding box
                    glDrawRectangle(impostor, vec2(0,0), vec2(1,1), true);
                }
                profile( timerQuery.stop(); profile.insert(material.name, move(timerQuery)); )
            }
        }

        //TODO: fog
        sky["inverseViewMatrix"] = view.inverse();
        sky[sky.sampler2D.first()] = 0; skymap.bind(0);
        glDrawRectangle(sky);

#if 1
        GLTexture color(width,height,RGB16F);
        framebuffer.blit(color);
        GLFrameBuffer::bindWindow(int2(position.x,window.size.y-height-position.y), size);
        present["framebuffer"]=0; color.bind(0);
        glDrawRectangle(present);
#endif

        profile( log(window.renderTime, lastProfile); lastProfile = move(profile); )
    }

} application;
