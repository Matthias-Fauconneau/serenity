#include "blender.h"
#include "process.h"
#include "string.h"
#include "data.h"
#include "map.h"
#include "time.h"
#include "jpeg.h"
#include "window.h"
#include "matrix.h"
#include "display.h"

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

/// from "An Efﬁcient and Robust Ray–Box Intersection Algorithm"
static bool intersect(vec3 min, vec3 max, vec3 O, vec3 D, float& t) {
    if(O>min && O<max) return true;
    float tmin = ((D.x >= 0?min:max).x - O.x) / D.x;
    float tmax = ((D.x >= 0?max:min).x - O.x) / D.x;
    float tymin= ((D.y >= 0?min:max).y - O.y) / D.y;
    float tymax= ((D.y >= 0?max:min).y - O.y) / D.y;
    if( (tmin > tymax) || (tymin > tmax) ) return false;
    if(tymin>tmin) tmin=tymin; if(tymax<tmax) tmax=tymax;
    float tzmin= ((D.z >= 0?min:max).z - O.z) / D.z;
    float tzmax= ((D.z >= 0?max:min).z - O.z) / D.z;
    if( (tmin > tzmax) || (tzmin > tmax) ) return false;
    if(tzmin>tmin) tmin=tzmin; if(tzmax<tmax) tmax=tzmax;
    t=tmax; return true;
}

/// from "Fast, Minimum Storage Ray/Triangle Intersection"
/*static bool intersect(vec3 A, vec3 B, vec3 C, vec3 O, vec3 D, float& t ) {
    if(dot(A-O,D)<=0 && dot(B-O,D)<=0 && dot(C-O,D)<=0) return false;
    vec3 edge1 = B - A;
    vec3 edge2 = C - A;
    vec3 pvec = cross( D, edge2 );
    float det = dot( edge1, pvec );
    if( det < 0.000001 ) return false;
    vec3 tvec = O - A;
    float u = dot(tvec, pvec);
    if(u < 0 || u > det) return false;
    vec3 qvec = cross( tvec, edge1 );
    float v = dot(D, qvec);
    if(v < 0 || u + v > det) return false;
    t = dot(edge2, qvec);
    t /= det;
    return true;
}*/

/// from "Yet Faster Ray-Triangle Intersection (using SSE4)"
struct Triangle { vec3 N; float nd; vec3 U; float ud; vec3 V; float vd; };
static_assert(sizeof(Triangle)==12*sizeof(float),"");
struct Hit { vec4 P; float t, u, v; };
#include "smmintrin.h"
static const float int_coef_arr[4] = { -1, -1, -1, 1 };
static const __m128 int_coef = _mm_load_ps(int_coef_arr);
static bool intersect(const vec4& O, const vec4& D, const Triangle &t, Hit &h) {
    const __m128 o = _mm_load_ps(&O.x);
    const __m128 d = _mm_load_ps(&D.x);
    const __m128 n = _mm_load_ps(&t.N.x);
    const __m128 det = _mm_dp_ps(n, d, 0x7f);
    const __m128 dett = _mm_dp_ps( _mm_mul_ps(int_coef, n), o, 0xff);
    const __m128 oldt = _mm_load_ss(&h.t);
    if((_mm_movemask_ps(_mm_xor_ps(dett, _mm_sub_ss(_mm_mul_ss(oldt, det), dett)))&1) == 0) { //t
        const __m128 detp = _mm_add_ps(_mm_mul_ps(o, det), _mm_mul_ps(dett, d));
        const __m128 detu = _mm_dp_ps(detp, _mm_load_ps(&t.U.x), 0xf1);
        if((_mm_movemask_ps(_mm_xor_ps(detu, _mm_sub_ss(det, detu)))&1) == 0) { //u
            const __m128 detv = _mm_dp_ps(detp, _mm_load_ps(&t.V.x), 0xf1);
            if((_mm_movemask_ps(_mm_xor_ps(detv, _mm_sub_ss(det, _mm_add_ss(detu, detv))))&1) == 0) { //v
                const __m128 inv_det = _mm_rcp_ss(det);
                _mm_store_ss(&h.t, _mm_mul_ss(dett, inv_det));
                _mm_store_ss(&h.u, _mm_mul_ss(detu, inv_det));
                _mm_store_ss(&h.v, _mm_mul_ss(detv, inv_det));
                _mm_store_ps(&h.P.x, _mm_mul_ps(detp, _mm_shuffle_ps(inv_det, inv_det, 0)));
                return true;
            }
        }
    }
    return false;
}

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
    /*bool operator==(const Vertex& o){
        const float e = 0x1.0p-16f;
        return sqr(position-o.position)<e && sqr(normal-o.normal)<e && sqr(texCoord-o.texCoord)<e;
    }*/
};
string str(const Vertex& v) { return "("_+str(v.position, v.normal, v.texCoord)+")"_; }


/// Parses a .blend file
struct BlendView : Widget {
    // View
    int2 lastPos; // last cursor position to compute relative mouse movements
    vec2 rotation=vec2(PI/3,-PI/3); // current view angles (yaw,pitch)
    float focalLength = 90; // current focal length (in mm for a 36mm sensor)
    const float zoomSpeed = 10; // in mm/click
    //Window window __(this, int2(1050,590), "BlendView"_);
    Window window __(this, int2(512,512), "BlendView"_);

    // File
    Folder folder;
    Map mmap; // Keep file mmaped
    const Scene* scene=0; // Blender scene (root handle to access all data)

    // Scene
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
    vec3 worldCenter=0; float worldRadius=0; // Scene bounding sphere in world space

    // Light
    vec3 lightMin=0, lightMax=0; // Scene bounding box in light space
    const float sunYaw = 4*PI/3;
    const float sunPitch = -PI/3;
    mat4 sun; // sun light transform

    // BVH
    struct Node { //TODO: SIMD
        static constexpr uint N = 4; // N=4: 128 | N=16 one cache line per component
        vec3 min[N];
        vec3 max[N];
        int children[N]; // 1bit: Node/Leaf, if node: 31bit index, if leaf [27bit index (2G/16), 4bit: (count-1)*4 (16x16 tris)], TODO: flag empty leaves
        uint axis[3], fill; // N=4: fit, N=16: 48bytes free
    };

    // Geometry
    struct Model {
        vec3 bbMin=__builtin_inff(), bbMax=-__builtin_inff(); //Local

        // for particle instancing (FIXME: use precomputed faces)
        array<Vertex> vertices;
        array<uint> indices;

        array<Node> BVH;
        array<Triangle> leafs;
        //array<uint> materialIndices; for each face

        array<mat4> instances; // World to object

        // Rendering order
        bool operator <(const Model& o) const { return instances.size()<o.instances.size(); }
    };

    array<Model> models;
#if 0
    //TODO: scene level BVH
    array<Node> BVH;
    struct Leaf { mat4 instance; /*Node* root; //shortcut*/ Model* model; };
#endif



    BlendView() : folder("Island"_,home()), mmap("Island.blend.orig", folder, Map::Read|Map::Write) { // with write access to fix pointers
        load();
        parse();

        window.clearBackground = false;
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

            for(Vertex& vertex: model.vertices) {
                // Computes object bounds
                model.bbMin=min(model.bbMin, vertex.position);
                model.bbMax=max(model.bbMax, vertex.position);

                if(scale<10) { // Ignores water plane
                    // Computes scene bounds in world space to fit view
                    vec3 position = transform*vertex.position;
                    worldMin=min(worldMin, position);
                    worldMax=max(worldMax, position);

                    // Compute scene bounds in light space to fit shadow
                    vec3 P = sun*position;
                    lightMin=min(lightMin,P);
                    lightMax=max(lightMax,P);
                }
            }

            ref<MLoop> indices(mesh.mloop, mesh.totloop);
            for(const MPoly& poly: ref<MPoly>(mesh.mpoly, mesh.totpoly)) {
                assert(poly.totloop==3 || poly.totloop==4);
                uint a=indices[poly.loopstart].v, b=indices[poly.loopstart+1].v;
                for(uint index: range(poly.loopstart+2, poly.loopstart+poly.totloop)) {
                    uint c = indices[index].v;
                    model.indices << a << b << c;

                    vec3 A = model.vertices[a].position, B = model.vertices[b].position, C = model.vertices[c].position;
                    Triangle t;
                    t.N = cross(B-A, C-A); t.nd = -dot(t.N, A);
                    float n2 = sqr(t.N);
                    t.U = cross(C-A, t.N) / n2; t.ud = -dot(t.U, A);
                    t.V = cross(t.N, B-A) / n2; t.vd = -dot(t.V, A);
                    model.faces << t;

                    b = c;
                }
            }
            assert(model.indices, "Empty model");

            /*if(mesh.mloopuv) { // Assigns UV coordinates to vertices
                ref<MLoopUV> texCoords(mesh.mloopuv, mesh.totloop);
                uint i=0;
                for(const MPoly& poly: ref<MPoly>(mesh.mpoly, mesh.totpoly)) {
                    {
                        vec2 texCoord = fract(vec2(texCoords[poly.loopstart].uv));
                        vec2& a = model.vertices[model.indices[i]].texCoord;
                        if(a && sqr(a-texCoord)>0.01) error("TODO: duplicate vertex");
                        a = texCoord;
                    }{
                        vec2 texCoord = fract(vec2(texCoords[poly.loopstart+1].uv));
                        vec2& b = model.vertices[model.indices[i+1]].texCoord;
                        if(b && sqr(b-texCoord)>0.01) error("TODO: duplicate vertex");
                        b = texCoord;
                    }
                    for(uint index: range(poly.loopstart+2, poly.loopstart+poly.totloop)) {
                        vec2 texCoord = fract(vec2(texCoords[index].uv));
                        vec2& c = model.vertices[model.indices[i+2]].texCoord;
                        if(c && sqr(c-texCoord)>0.01) error("TODO: duplicate vertex");
                        c = texCoord;
                        i += 3; // Keep indices counter synchronized with MPoly
                    }
                }
            }*/

            bool render = scene->lay&object.lay; // Visible layers
            for(const ParticleSystem& particle: object.particlesystem) if(!(particle.part->draw&ParticleSettings::PART_DRAW_EMITTER)) render=false;
            if(render) model.instances << transform.inverse(); // Appends an instance
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
                for(int n=0; n</*particle.totpart/200*//*256*//*16*/1;) {
                    const array<uint>& indices = model.indices;
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
                    vec3 A = model.vertices[a].position,
                            B = model.vertices[b].position,
                            C = model.vertices[c].position;
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
                    dupli.instances << transform.inverse();
                    n++;
                }
            }
        }

        for(uint i=0; i<models.size();) {
            Model& model = models[i];
            if(!model.instances) { models.removeAt(i); continue; } else i++;
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

    void render(int2 unused position, int2 unused size) override {
        // Computes view transform
        uint width = size.x, height = size.y;
        mat4 view;
        //view.perspective(2*atan(36/(2*focalLength)), width, height, 1./4, 4); //FIXME
        view.scale(2.f/worldRadius); // fit scene (isometric approximation)
        view.translate(vec3(0,0,-worldRadius)); // step back
        view.rotateX(rotation.y); // pitch
        view.rotateZ(rotation.x); // yaw
        view.translate(vec3(0,0,-worldCenter.z));

        mat4 world = view.inverse();
        for(uint y : range(height)) for(uint x : range(width)) {
            vec3 viewPosition = vec3(2.0*x/width-1, 1-2.0*y/height, 0);
            vec3 viewRay = vec3(0,0,1);
            //FIXME: perspective division ?
            /*vec3 worldPosition = world * vec3(screen.x, screen.y, -1);
            vec3 worldRay = normalize(world * vec3(screen.x, screen.y, 1) - worldPosition ); //FIXME: complicated?*/
            vec3 worldPosition = world * viewPosition;
            vec3 worldRay = normalize( (mat3)view.transpose() * viewRay );

            Hit hit; hit.t =  -__builtin_inff();
            for(Model& model: models) {
                if(model.instances.size()>1) continue;
                if(model.indices.size() > 96774) continue;
                for(const mat4& instance : model.instances) {
                    vec4 objectPosition = vec4(instance * worldPosition, 1.f);
                    vec4 objectRay = vec4( (mat3)instance * worldRay, 0.f);
                    float t=0;
                    if(intersect(model.bbMin,model.bbMax,objectPosition.xyz(),objectRay.xyz(), t) && t>hit.t) {
                        for(const Triangle& t: model.faces.slice(0, min(256u,model.faces.size()))) intersect(objectPosition, objectRay, t, hit);
                    }
                }
            }
            framebuffer(x,y) = byte4(clip<int>(0,-hit.t/worldRadius/2*0xFF,0xFF)); //xyz | normal
        }
    }
} application;
