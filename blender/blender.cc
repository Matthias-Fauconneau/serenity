#include "blender.h"
#include "thread.h"
#include "string.h"
#include "data.h"
#include "map.h"
#include "time.h"
#include "window.h"
#include "gl.h"
#include "matrix.h"
//#include "jpeg.h"
FILE(blender)

// Quicksort
generic uint partition(array<T>& at, int left, int right, int pivotIndex) {
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
generic void quicksort(array<T>& at, int left, int right) {
    if(left < right) { // If the list has 2 or more items
        int pivotIndex = partition(at, left, right, (left + right)/2);
        if(pivotIndex) quicksort(at, left, pivotIndex-1);
        quicksort(at, pivotIndex+1, right);
    }
}
/// Quicksorts the array in-place
generic void quicksort(array<T>& at) { if(at.size) quicksort(at, 0, at.size-1); }

/// Used to fix pointers in .blend file-block sections
struct Block {
    uint64 begin; // First memory address used by pointers pointing in this block
    uint64 end; // Last memory address used by pointers pointing in this block
    int64 delta; // Target memory address where the block was loaded - begin
};
bool operator<(const Block& a, const Block& b) { return a.begin<b.begin; }
String str(const Block& b) { return str(hex(b.begin))+str("-")+str(hex(b.end)); }

/// SDNA type definitions
struct Struct {
    string name;
    uint size;
    struct Field {
        string typeName;
        uint reference;
        string name;
        uint count;
        const Struct* type;
    };
    array<Field> fields;
};
inline bool operator==(const Struct& a, const string& b) { return a.name==b; }

string elementType(const Struct& o, const Struct::Field& f){ // HACK: DNA doesn't define ListBase element types
    static array<string> listbases =split(
                "bNodeTree.nodes:bNode bNode.inputs:bNodeSocket bNode.outputs:bNodeSocket "
                "ParticleSettings.dupliweights:ParticleDupliWeight  Object.particlesystem:ParticleSystem Scene.base:Base"_);
    for(string listbase: listbases) { TextData s(listbase); if(s.until('.')==o.name && s.until(':')==f.name) return s.untilEnd(); }
    return ""_;
}

String str(const Struct& o, const array<const Struct*>& defined={}) {
    String s = "struct "_+o.name+" {\n"_;
    for(const Struct::Field& f: o.fields)  {
        s<<"    "_+(defined.contains(f.type)?""_:"struct "_)+(f.typeName=="uint64_t"_?"uint64"_:f.typeName);
        if(f.typeName=="ListBase"_) s<<"<"_+elementType(o,f)+">"_;
        s<<repeat("*"_,f.reference)+" "_+f.name;
        if(f.count!=1) s<<"["_+str(f.count)+"]"_;
        s<<";\n"_;
    }
    return s+"};"_;
}

// HACK: DNA doesn't define enums
enum class ObjectType { Empty, Mesh, Curve, Surf, Font, MBall, Lamp=10, Camera };
enum { PART_DRAW_EMITTER=8 };
enum { PSYS_VG_DENSITY=0 };
enum { SOCK_CUSTOM=-1, SOCK_FLOAT, SOCK_VECTOR, SOCK_RGBA, SOCK_SHADER, SOCK_BOOLEAN, SOCK_INT=6, SOCK_STRING	};
enum { SH_NODE_MATERIAL=100, SH_NODE_RGB, SH_NODE_VALUE, SH_NODE_MIX_RGB, SH_NODE_VALTORGB, SH_NODE_RGBTOBW,
       SH_NODE_TEXTURE, SH_NODE_NORMAL, SH_NODE_GEOMETRY, SH_NODE_MAPPING, SH_NODE_CURVE_VEC, SH_NODE_CURVE_RGB,
       SH_NODE_CAMERA=114, SH_NODE_MATH, SH_NODE_VECT_MATH, SH_NODE_SQUEEZE, SH_NODE_MATERIAL_EXT, SH_NODE_INVERT,
       SH_NODE_SEPRGB, SH_NODE_COMBRGB, SH_NODE_HUE_SAT,NODE_DYNAMIC,SH_NODE_OUTPUT_MATERIAL, SH_NODE_OUTPUT_WORLD,
       SH_NODE_OUTPUT_LAMP, SH_NODE_FRESNEL, SH_NODE_MIX_SHADER, SH_NODE_ATTRIBUTE, SH_NODE_BACKGROUND,
       SH_NODE_BSDF_ANISOTROPIC, SH_NODE_BSDF_DIFFUSE, SH_NODE_BSDF_GLOSSY, SH_NODE_BSDF_GLASS,
       SH_NODE_BSDF_TRANSLUCENT=137, SH_NODE_BSDF_TRANSPARENT, SH_NODE_BSDF_VELVET, SH_NODE_EMISSION,
       SH_NODE_NEW_GEOMETRY, SH_NODE_LIGHT_PATH, SH_NODE_TEX_IMAGE, SH_NODE_TEX_SKY=145, SH_NODE_TEX_GRADIENT,
       SH_NODE_TEX_VORONOI, SH_NODE_TEX_MAGIC, SH_NODE_TEX_WAVE, SH_NODE_TEX_NOISE, SH_NODE_TEX_MUSGRAVE=152,
       SH_NODE_TEX_COORD=155, SH_NODE_ADD_SHADER, SH_NODE_TEX_ENVIRONMENT, SH_NODE_OUTPUT_TEXTURE, SH_NODE_HOLDOUT,
       SH_NODE_LAYER_WEIGHT, SH_NODE_VOLUME_TRANSPARENT, SH_NODE_VOLUME_ISOTROPIC, SH_NODE_GAMMA, SH_NODE_TEX_CHECKER,
       SH_NODE_BRIGHTCONTRAST, SH_NODE_LIGHT_FALLOFF, SH_NODE_OBJECT_INFO, SH_NODE_PARTICLE_INFO, SH_NODE_TEX_BRICK,
       SH_NODE_BUMP, SH_NODE_SCRIPT, SH_NODE_AMBIENT_OCCLUSION, SH_NODE_BSDF_REFRACTION, SH_NODE_TANGENT,
       SH_NODE_NORMAL_MAP, SH_NODE_HAIR_INFO, SH_NODE_SUBSURFACE_SCATTERING, SH_NODE_WIREFRAME, SH_NODE_BSDF_TOON,
       SH_NODE_WAVELENGTH };

// Renderer definitions
struct Vertex { // Vertex6
    vec3 position; // World-space position
    vec3 normal; // World-space vertex normals
    bool operator==(const Vertex& o){
        const float e = 0x1.0p-16f;
        return sq(position-o.position)<e && sq(normal-o.normal)<e;
    }
};
String str(const Vertex& v) { return "("_+str(v.position, v.normal)+")"_; }

struct Vertex7 {
    vec3 position;
    vec2 texcoords;
    vec3 zVector;
};

struct Node;

struct Input {
    Input(string name, shared<Node>&& node, string output):name(name),node(move(node)), output(output){}
    Input(string name, vec4 value):name(name),value(value){}

    String toGLSL(String& global) const;

    string name;
    shared<Node> node = 0;
    string output;
    vec4 value = 0;
};
String str(const Input& o) {
    String s;
    /**/  if(o.node) s<<str(o.node);
    else {
        if(o.name!="Value"_) s<<o.name<<":"_;
        if(o.value.x==o.value.y && o.value.x==o.value.z && o.value.x==o.value.w) s<<str(o.value.x);
        else s<<str(o.value);
    }
    return s;
}

struct Node : shareable {
    virtual String toGLSL(String&, string output) const { error("Unimplemented", output, str()); }
    virtual String str() const { String s; s<<name; if(inputs) s<<"("_+::str(inputs)+")"_; return s; }

    string name;
    array<Input> inputs;
};
String str(const Node& o) { static int prefix=0; prefix++; String s = "\n"_+repeat(" "_,prefix)+o.str(); prefix--; /*Assumes single line*/  return s; }
String Input::toGLSL(String& global) const { return node ? node->toGLSL(global, output):"vec4"_+::str(value); }

struct ValueNode : Node {
    ValueNode(float value):value(value){}
    String str() const override { return ::str(value); }
    String toGLSL(String&, string) const override { return ::str(value); }
    float value;
};

enum Operation { Add, Sub, Mul, Div, Sin, Cos, Tan, ASin, ACos, ATan, Power, Log, Min, Max, Round, Less, More, Modulo };
static const ref<string> operationNames {"add"_, "sub"_, "mul"_, "div"_, "sin"_, "cos"_, "tan"_, "asin"_, "acos"_, "atan"_,
            "power"_, "log"_, "min"_, "max"_, "round"_, "less"_, "more"_, "modulo"_};
struct MathNode : Node {

    MathNode(short operation):operation(Operation(operation)){}
    String str() const override {
        assert(operation<operationNames.size, (int)operation);
        return operationNames[operation]+"("_+::str(inputs)+")"_;
    }
    String toGLSL(String& global, string) const override {
        return operationNames[operation]+"("_+(inputs[0].node?inputs[0].toGLSL(global): ::str(inputs[0].value.x))
                    +(inputs.size==2?", "_+(inputs[1].node?inputs[1].toGLSL(global): ::str(inputs[1].value.x)):String())+")"_;
    }
    Operation operation;
};

enum VectorOperation { VectorAdd, VectorSubstract, NormalizeAdd, Dot, Cross, Normalize };
static const ref<string> vectorOperationNames {"addVector"_, "subVector"_, "normalizeadd"_, "dot"_,"cross"_,"normalize"};
struct VectorMathNode : Node {
    VectorMathNode(short operation):operation(VectorOperation(operation)){}
    String str() const override {
        assert(operation<vectorOperationNames.size, (int)operation);
        return vectorOperationNames[operation]+"("_+::str(inputs)+")"_;
    }
    String toGLSL(String& global, string) const override {
        if(operation==Dot && inputs[1].value==vec4(0,0,1,0)) return inputs[0].toGLSL(global)+".z"_;
        return vectorOperationNames[operation]+"("_+inputs[0].toGLSL(global)+(inputs.size==2?", "_+inputs[1].toGLSL(global):String())+")"_;
    }
    VectorOperation operation;
};

struct MixRGBNode : Node {
    String toGLSL(String& global, string) const override {
        return "mix("_+inputs[1].toGLSL(global)+","_+inputs[2].toGLSL(global)+", "_+inputs[0].toGLSL(global)+")"_;
    }
};

struct GeometryNode : Node {
    String toGLSL(String&, string output) const override{
        /**/  if(output=="Position"_) return String("vec4(vPosition, 0)"_);
        else if(output=="Normal"_) return String("vec4(nodeNormal, 0)"_);
        else error(output);
    }
};
struct MappingNode : Node {
    MappingNode(const mat4& transform) : transform(transform) {}
    String toGLSL(String& global, string) const override{
        mat4 scale; scale.scale(transform(0,0)); assert(transform==scale, transform); // Uniform scale
        return ::str(transform(0,0))+"*"_+inputs[0].toGLSL(global);
    }
    mat4 transform;
};

struct SeparateRGBNode : Node {
    String toGLSL(String& global, string output) const override {
        /**/  if(output=="R"_) return inputs[0].toGLSL(global)+".r"_;
        else if(output=="G"_) return inputs[0].toGLSL(global)+".g"_;
        else if(output=="B"_) return inputs[0].toGLSL(global)+".b"_;
        else error(output);
    }
};
struct BrightContrastNode : Node {
    String toGLSL(String& global, string) const override {
        return "brightness_contrast("_+
                inputs[0].toGLSL(global)+", "_+
                ::str(inputs[1].value.x)+", "_+
                ::str(inputs[2].value.x)+")"_;
    }
};
struct HueSaturationNode : Node {
    String toGLSL(String& global, string) const override {
        return "hue_sat("_+
                ::str(inputs[0].value.x)+", "_+
                ::str(inputs[1].value.x)+", "_+
                ::str(inputs[2].value.x)+", "_+
                inputs[4].toGLSL(global)+")"_;
    }
};

struct MixShaderNode : Node {
    String toGLSL(String& global, string) const override {
        if(inputs[1].node->name=="ShaderNodeBsdfTransparent"_) { //HACK
            return "vec4("_+inputs[2].toGLSL(global)+".rgb, "_+inputs[0].toGLSL(global)+")"_;
        }
        return "mix("_+inputs[1].toGLSL(global)+","_+inputs[2].toGLSL(global)+", "_+inputs[0].toGLSL(global)+")"_;
    }
};

struct BSDFNode : Node {
    String toGLSL(String& global, string) const override{ return inputs[0].toGLSL(global); }
    //String str() const override { return ::str(inputs[0]); }
};
struct BsdfDiffuseNode : BSDFNode {};
struct BsdfTranslucentNode : BSDFNode {};
struct BsdfTransparentNode : BSDFNode {
    //String toGLSL(String&, string) const override{ return String("vec4(0)"_); }
};
struct BsdfGlassNode : BSDFNode {};

struct TexCoordNode : Node {
    String toGLSL(String&, string output) const override{
        if(output=="Generated"_) return String("vec4(objectPosition.xyz,1)"_);
        else if(output=="UV"_) return String("vTexCoords"_);
        else error(output);
    }
};
struct TexImageNode : Node {
    TexImageNode(const string& name) : name(replace(replace(replace(section(section(name,'/',-2,-1),'.')," "_,""_),"("_,""_),")"_,""_)) {}

    String toGLSL(String& global, string) const override{
        global<<"uniform sampler2D "_+name+";\n"_;
        return "texture("_+name+", "_+(inputs[0].node?inputs[0].toGLSL(global):String("vTexCoords"_))+".xy)"_;
    }
    String str() const override { return name+"("_+::str(inputs)+")"_; }

    String name;
};
struct TexNoiseNode : Node {
    String toGLSL(String&, string) const override {
        return String("0"_); // Stub
    }
};

/// Parses Blender nodes
shared<Node> parse(const bNode& o) {
    shared<Node> node = 0;
    //FIXME: automatic map (factory)
    /**/  if(o.type==SH_NODE_VALUE) node = shared<ValueNode>(o.outputs.first->ns.vec[0]);
    else if(o.type==SH_NODE_MIX_RGB) node = shared<MixRGBNode>();
    else if(o.type==SH_NODE_MATH) node = shared<MathNode>(o.custom1);
    else if(o.type==SH_NODE_VECT_MATH) node = shared<VectorMathNode>(o.custom1);
    else if(o.type==SH_NODE_NEW_GEOMETRY) node = shared<GeometryNode>();
    else if(o.type==SH_NODE_MAPPING) node = shared<MappingNode>((mat4&)((TexMapping*)o.storage)->mat);
    else if(o.type==SH_NODE_SEPRGB) node = shared<SeparateRGBNode>();
    else if(o.type==SH_NODE_BRIGHTCONTRAST) node = shared<BrightContrastNode>();
    else if(o.type==SH_NODE_HUE_SAT) node = shared<HueSaturationNode>();
    else if(o.type==SH_NODE_MIX_SHADER) node = shared<MixShaderNode>();
    else if(o.type==SH_NODE_BSDF_DIFFUSE) node = shared<BsdfDiffuseNode>();
    else if(o.type==SH_NODE_BSDF_TRANSLUCENT) node = shared<BsdfTranslucentNode>();
    else if(o.type==SH_NODE_BSDF_TRANSPARENT) node = shared<BsdfTransparentNode>();
    else if(o.type==SH_NODE_BSDF_GLASS) node = shared<BsdfGlassNode>();
    else if(o.type==SH_NODE_TEX_COORD) node = shared<TexCoordNode>();
    else if(o.type==SH_NODE_TEX_IMAGE) { node = shared<TexImageNode>(str((const char*)(((bImage*)o.id)->name))); }
    else if(o.type==SH_NODE_TEX_NOISE) node = shared<TexNoiseNode>();
    else error(o.type, o.idname, o.name);
    node->name = section(str(o.idname),'.');
    for(const bNodeSocket& socket: o.inputs) {
        if(socket.link) {
            node->inputs << Input(str(socket.name), parse(*socket.link->fromnode), str((const char*)socket.link->fromsock->identifier));
        } else {
            /**/  if(socket.type==SOCK_FLOAT) node->inputs << Input(str(socket.name), socket.ns.vec[0]);
            else if(socket.type==SOCK_VECTOR) node->inputs << Input(str(socket.name), vec4(socket.ns.vec));
            else if(socket.type==SOCK_RGBA) node->inputs << Input(str(socket.name), vec4(socket.ns.vec));
            else error(node->name, socket.name, socket.type, socket.identifier, socket.name, socket.idname);
        }
    }
    return node;
}

struct Shader : array<GLTexture> {
    Shader(){}
    Shader(const string& name, GLShader&& shader):name(name),shader(move(shader)){}
    String name;
    bool blendAlpha = false;
    GLShader shader;
};

struct Surface {
    Surface(string name):name(name){}
    String name;
    String glsl;
    Shader shader; // Depends on instance count
    array<uint> indices;
    array<Vertex> vertices;
    GLIndexBuffer indexBuffer;
    GLVertexBuffer vertexBuffer;
};

struct Model {
    mat4 transform;

    array<Vertex> vertices;
    array<Surface> surfaces;
    array<mat4> instances;
    array<GLTexture> impostors; //FIXME: share
    GLVertexBuffer impostorsVertices;
    GLIndexBuffer impostorsIndices;
    uint impostorIndex=0;

    // Rendering order
    bool operator<(const Model& o) const { return instances.size<o.instances.size; }
};
const uint impostorMin = 12;

/// Parses a .blend file
struct BlendView : Widget { //FIXME: split Scene (+split generic vs blender specific) vs View
    // View
    int2 lastPos; // last cursor position to compute relative mouse movements
    vec2 rotation=vec2(-2*PI/3,-PI/3); // current view angles (yaw,pitch)
#define PERSPECTIVE 1
#if PERSPECTIVE
    float focalLength = 90; // current focal length (in mm for a 36mm sensor)
    const float zoomSpeed = 10; // in mm/click
#else
    float viewScale = 4;
    const float zoomSpeed = pow(2,1./4); /*/in x/click*/
#endif
    Window window {this, int2(1050,590), "BlendView"_,  Image(), Window::OpenGL};

    // File
    Folder folder;
    Map file;
    const Scene* scene=0; // Blender scene (root handle to access all data)

    // Renderer
    GLFrameBuffer framebuffer;
    GLTexture resolvedBuffer;
    GLShader present {blender(),{"screen present"_}};

    // Scene
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
    vec3 worldCenter=0; float worldRadius=0; // Scene bounding sphere in world space

    // Light
    vec3 lightMin=0, lightMax=0; // Scene bounding box in light space
    mat4 sun; // sun light transform
    GLFrameBuffer sunShadow;
    GLShader shadow {blender(),{"transform"_}};

    // Sky
    GLShader sky {blender(),{"skymap"_}};
    GLTexture skymap;
    GLVertexBuffer vertexBuffer;

    // Geometry
    array<Model> models;
    //GLShader bake {blender(),{"transform normal bake"_}};
    GLShader impostor {blender(),{"transform impostor diffuse leafs1shader"_}}; //FIXME

    // Timer
    Time time; float frameTime = 40./1000; uint frameCount=0;

    BlendView() : folder("Island"_,home()) {
        vertexBuffer.upload<vec2>({vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1)});

        assert_(existsFile("Island.blend"_, folder), folder.name());
        file = Map(File("Island.blend"_, folder), Map::Prot(Map::Read|Map::Write), Map::Flags::Private);  // with write access to fix pointers
        load();
        parse();

        // FIXME: parse from .blend
        skymap = GLTexture(decodeImage(readFile(String(sky.sampler2D.first()+".png"_), folder)), SRGB|Bilinear);

        window.localShortcut(Escape).connect([]{exit();});
        window.clearBackground = false;
        glDepthTest(true);
        //glCullFace(true);
        window.show();
    }

    /// Lists all definitions needed to define a type (recursive)
    void listDependencies(const Struct& type, array<const Struct*>& deps) {
        if(!deps.contains(&type)) {
            deps << &type;
            for(const Struct::Field& field: type.fields) if(!field.reference) listDependencies(*field.type, deps);
        }
    }
    array<const Struct*> listDependencies(const Struct& type) { array<const Struct*> deps; listDependencies(type,deps); return deps; }

    /// Outputs structures DNA to be copied to a C header in order to update this parser to new Blender versions //FIXME: own class
    String DNAtoC(const Struct& type, array<const Struct*>& defined, array<const Struct*>& defining, array<Struct>& structs) {
        String s;
        assert_(!defining.contains(&type));
        defining << &type;
        array<const Struct*> deferred;
        for(const Struct::Field& field: type.fields) {
            const Struct* fieldType = field.type;
            if(field.typeName=="ListBase"_) {
                string elementType = ::elementType(type,field);
                if(elementType) {
                    assert(structs.contains(elementType), elementType);
                    fieldType = &structs[structs.indexOf(elementType)];
                }
            }
            if(fieldType) {
                if(fieldType==&type) continue; // Self reference
                if(defined.contains(fieldType)) continue; // Already defined
                if(deferred.contains(fieldType)) continue; // Already deferred
                if(field.reference || field.typeName=="ListBase"_) { // Only defines references if we can
                    array<const Struct*> deps = listDependencies(*fieldType);
                    for(const Struct* d: deps) if(defining.contains(d)) { if(d==&type) deferred << fieldType; goto continue_2; }
                }
                s << DNAtoC(*fieldType, defined, defining, structs);
            }
           continue_2:;
        }
        s << str(type,defined) << "\n"_;
        defined << &type;
        defining.pop();
        for(const Struct* d: deferred) s << DNAtoC(*d, defined, defining, structs);
        return s;
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
                for(const Block& block: blocks/*.slice(blocks.binarySearch( Block{pointer,0,0} )-1)*/) {
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
                                for(const Block& block: blocks/*.slice(blocks.binarySearch( Block{pointer,0,0} )-1)*/) {
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

        BinaryData file(this->file);
        file.seek(12); //Assumes 64bit little endian
        blocks.reserve(32768);
        while(file) { // Parses SDNA
            const BlockHeader& header = file.read<BlockHeader>();
            string identifier(header.identifier,4);
            BinaryData data( file.Data::read(header.size) );
            assert(blocks.size<blocks.capacity); // Avoids large reallocations
            blocks << Block{header.address, header.address+header.size, int64(uint64(data.buffer.data)-header.address)};
            if(identifier == "DNA1"_) {
                data.advance(4); //SDNA
                data.advance(4); //NAME
                uint nameCount = data.read();
                array< string > names;
                for(uint unused i: range(nameCount)) names << data.untilNull();
                data.align(4);
                data.advance(4); //TYPE
                uint typeCount = data.read();
                array< string > types;
                for(uint unused i: range(typeCount)) types << data.untilNull();
                data.align(4);
                data.advance(4); //TLEN
                ref<uint16> lengths = data.read<uint16>(typeCount);
                data.align(4);
                data.advance(4); //STRC
                uint structCount = data.read();
                assert_(!structs);
                for(uint unused i: range(structCount)) {
                    uint16 index = data.read();
                    string name = types[index];
                    uint size = lengths[index];
                    uint16 fieldCount = data.read();
                    array<Struct::Field> fields;
                    for(uint unused i: range(fieldCount)) {
                        string type = types[(uint16)data.read()];
                        string name = names[(uint16)data.read()];
                        uint reference=0, count=1;
                        TextData s (name);
                        if(s.match("(*"_)) { // Parses function pointers
                            name.data+=2; name.size-=2+3;
                            type = "void"_; reference++;
                        } else {
                            while(s.match('*')) { // Parses references
                                name.data++; name.size--;
                                reference++;
                            }
                        }
                        s.whileNot('['); if(s.match('[')) { // Parses static arrays
                            name.size -= 1+(s.buffer.size-s.index);
                            count = s.integer();
                            s.match(']');
                            while(s.match('[')) { // Flattens multiple indices
                                count *= s.integer();
                                s.match(']');
                            }
                        }
                        fields << Struct::Field{type, reference, name, count, 0};
                    }
                    structs << Struct{name, size, move(fields)};
                }
                structs << Struct{"void"_,0,{}} << Struct{"char"_,1,{}} << Struct{"short"_,2,{}} << Struct{"int"_,4,{}} << Struct{"uint64_t"_,8,{}}
                        << Struct{"float"_,4,{}} << Struct{"double"_,8,{}}; // Do not move this as DNA structs are indexed by BlockHeader
                for(Struct& s: structs) {
                    for(Struct::Field& f: s.fields) {
                        int index = structs.indexOf(f.typeName);
                        if(index>=0) f.type = &structs[index]; // Structs may not be reallocated after taking these references
                        else assert(f.reference);
                    }
                }
            }
            if(identifier == "ENDB"_) break;
        }
        //quicksort(blocks);

        file.seek(0);
        string version = file.read<byte>(12);
        if(version!=sdnaVersion || false) {
            array<const Struct*> defined;
            defined << &structs[structs.indexOf("void"_)]
                    << &structs[structs.indexOf("char"_)] << &structs[structs.indexOf("short"_)] << &structs[structs.indexOf("int"_)]
                    << &structs[structs.indexOf("uint64_t"_)] << &structs[structs.indexOf("float"_)] << &structs[structs.indexOf("double"_)]
                    << &structs[structs.indexOf("ID"_)] << &structs[structs.indexOf("ListBase"_)]; // ListBase is redefined with a template
            array<const Struct*> defining;
            String header = "#pragma once\n#include \"sdna.h\"\nconst string sdnaVersion = \""_+version+"\"_;\n"_;
            header << DNAtoC(structs[structs.indexOf("Scene"_)], defined, defining, structs);
            header << DNAtoC(structs[structs.indexOf("NodeTexImage"_)], defined, defining, structs);
            header = replace(header,"Material"_,"bMaterial"_); //FIXME: whole words only
            header = replace(header,"Image"_,"bImage"_); //FIXME: whole words only
            header = replace(header,"Key"_,"bKey"_); //FIXME: whole words only
            error(header);
        }
        while(file) { // Fixes pointers
            const BlockHeader& header = file.read<BlockHeader>();
            string identifier(header.identifier,4);
            BinaryData data (file.Data::read(header.size));
            const Struct& type = structs[header.type];

            if(identifier == "SC\0\0"_) scene = (Scene*)data.buffer.data;
            else if(identifier == "DNA1"_ ) continue;
            else if(identifier == "ENDB"_) break;

            if(header.size >= header.count*type.size && type.fields && header.type != 0) {
                assert(header.size == header.count*type.size);
                for(uint unused i: range(header.count)) fix(blocks, type, data.Data::read(type.size));
            }
        }
    }

    /// Extracts relevant data from Blender structures
    void parse() {
        // Parses objects
        map<const Object*, uint> modelIndex;
        for(const Base& base: scene->base) {
            assert(&base, scene->base.first);
            const Object& object = *base.object;
            Model model;

            mat4 transform;
            for(uint i: range(4)) for(uint j: range(4)) transform(i,j) = object.obmat[j*4+i];

            if(ObjectType(object.type) == ObjectType::Lamp) {
                assert(sun==mat4());
                assert(object.rotmode==1);
                sun.rotateX(object.rot[0]);
                sun.rotateY(object.rot[1]);
                sun.rotateZ(object.rot[2]);
                continue;
            }
            if(ObjectType(object.type) != ObjectType::Mesh) continue;
            const Mesh& mesh = *(Mesh*)object.data;
            if(!mesh.mvert) continue;
            float scale = norm(vec3(transform(0,0),transform(1,1),transform(2,2)));
            if(scale>10) continue; // Currently ignoring water plane, TODO: water reflection

            ref<MVert> verts(mesh.mvert, mesh.totvert);
            for(uint i: range(verts.size)) {
                const MVert& vert = verts[i];
                vec3 position = vec3(vert.co);
                vec3 normal = normalize(vec3(vert.no[0],vert.no[1],vert.no[2]));
                model.vertices << Vertex {position, normal};
                i++;
            }

            vec3 objectMin=model.vertices[0].position, objectMax=model.vertices[0].position;
            for(Vertex& vertex: model.vertices) {
                // Computes object bounds
                objectMin=min(objectMin, vertex.position);
                objectMax=max(objectMax, vertex.position);
            }
            // Scales positions and translates to keep unit bounding box [-1,1] in object space
            for(Vertex& vertex: model.vertices) {
                vertex.position = (vertex.position-(objectMin+objectMax)/2.f)/((objectMax-objectMin)/2.f);
            }
            transform.translate((objectMin+objectMax)/2.f);
            transform.scale((objectMax-objectMin)/2.f);
            model.transform = transform; //for emitter and particles

            for(int materialIndex: range(mesh.totcol)) {
                const bMaterial& material = *mesh.mat[materialIndex];
                String name = replace(replace(simplify(toLower(str(material.id.name+2)))," "_,"_"),"."_,"_"_);
                Surface surface(name);
                if(!find(blender(), surface.name)) { //FIXME: parse known tags once for all
                    if(!material.use_nodes) continue; // Only supports node shaders
                    static String helpers = readFile("gpu_shader_material.glsl"_,folder);
                    assert(material.use_nodes, name);
                    bNodeTree* tree = material.nodetree;
                    shared<Node> output;
                    for(const bNode& node: tree->nodes) {
                        if(node.type==SH_NODE_OUTPUT_MATERIAL)
                            for(const bNodeSocket& socket: node.inputs) {
                                if(str(socket.name)=="Surface"_) output = ::parse(*socket.link->fromnode);
                            }
                    }
                    String global;
                    String local = output->toGLSL(global, "Color"_);
                    surface.glsl = helpers+global+replace(blender(),"%node"_,local);
                }

                ref<MLoop> indices(mesh.mloop, mesh.totloop);
                for(const MPoly& poly: ref<MPoly>(mesh.mpoly, mesh.totpoly)) {
                    assert(poly.totloop==3 || poly.totloop==4);
                    if(poly.mat_nr!=materialIndex) continue;
                    uint a=indices[poly.loopstart].v, b=indices[poly.loopstart+1].v;
                    for(uint index: range(poly.loopstart+2, poly.loopstart+poly.totloop)) {
                        uint c = indices[index].v;
                        surface.indices << a << b << c;
                        b = c;
                    }
                }
                assert(surface.indices, "Empty surface");

                model.surfaces << move(surface);
            }

            bool render = scene->lay&object.lay; // Visible layers
            for(const ParticleSystem& particle: object.particlesystem) if(!(particle.part->draw&PART_DRAW_EMITTER)) render=false;
            if(render) model.instances << transform; // Appends an instance
            modelIndex.insert(&object, models.size); // Keep track of Object <-> Model mapping (e.g to instanciate particles)
            models << move(model);
        }

        // Parses particle systems
        for(const Base& base: scene->base) {
            for(const ParticleSystem& particle: base.object->particlesystem) {
                Model& model = models[modelIndex.at(base.object)];
                const ParticleSettings& p = *particle.part;
                if(p.brownfac>10) continue; // FIXME: render birds with brownian dispersion

                // Selects N random faces
                Random random;
                for(int n=0; n</*particle.totpart*/1024;) {
                    assert(model.surfaces.size==1);
                    const array<uint>& indices = model.surfaces[0].indices;
                    uint index = random % (indices.size/3) * 3;
                    uint a=indices[index], b=indices[index+1], c=indices[index+2];
                    uint densityVG = particle.vgroup[PSYS_VG_DENSITY];
                    if(densityVG) {
                        const Mesh& mesh = *(Mesh*)base.object->data; //FIXME: tagged union
                        if(mesh.dvert[a].dw[densityVG-1].weight < 0.5) continue;
                        if(mesh.dvert[b].dw[densityVG-1].weight < 0.5) continue;
                        if(mesh.dvert[c].dw[densityVG-1].weight < 0.5) continue;
                    }

                    mat4 transform;
                    // Translates
                    vec3 A = model.transform*model.vertices[a].position,
                            B = model.transform*model.vertices[b].position,
                            C = model.transform*model.vertices[c].position;
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
                        for(const ParticleDupliWeight& w: p.dupliweights) total += w.count;
                        uint index = random % total;
                        uint i = 0;
                        for(const ParticleDupliWeight& w: p.dupliweights) {
                            if(index >= i && index<i+w.count) { object = w.ob; break; }
                            i+=w.count;
                        }
                    }

                    Model& dupli = models.at(modelIndex.at(object));
                    dupli.instances << transform*dupli.transform;
                    n++;
                }
            }
        }
        quicksort(models); // Sorts by rendering cost (i.e render most instanced object last)

        for(uint i=0; i<models.size;) {
            Model& model = models[i];
            if(!model.surfaces || !model.instances) { models.removeAt(i); continue; } else i++;

            for(Surface& surface : model.surfaces) {
                // Remap surface indices from shared model vertices to local surface vertices
                buffer<uint> remap(model.vertices.size, model.vertices.size, -1);
                array<uint> indices = move(surface.indices);
                surface.indices = array<uint>(surface.indices.size);
                surface.vertices = array<Vertex>(model.vertices.size);
                //uint duplicates=0;
                for(uint index: indices) {
                    uint& newIndex = remap[index];
                    if(newIndex == uint(-1)) {
                        Vertex v = model.vertices[index];
                        /*for(uint i: range(surface.vertices.size)) { const Vertex& o = surface.vertices[i]; // 6% duplicates on tree (FIXME: grid)
                            if(sq(v.position-o.position)<0x1.0p-16 && sq(v.normal-o.normal)<0x1.0p-16) {
                                duplicates++;
                                newIndex = i;
                                goto break_;
                            }
                        }*/ /*else*/ {
                            newIndex = surface.vertices.size;
                            surface.vertices << v;
                        }
                        //break_:;
                    }
                    surface.indices << newIndex;
                }
                //log(model.vertices.size, surface.vertices.size, duplicates);

                // Uploads geometry
                surface.vertexBuffer.upload(surface.vertices);
                surface.indexBuffer.upload(surface.indices);

                // Compiles shader
                if(surface.glsl) { // Custom node shader
                    error(surface.name, surface.glsl);
                    surface.shader = Shader(surface.name,  GLShader(surface.glsl,{"transform normal diffuse light node "_}));
                } else { // Hardcoded override
                    String glsl = replace(blender(),"%instanceCount"_,str(model.instances.size));
                    String tags = "transform normal diffuse light "_+surface.name;
                    surface.shader = Shader(surface.name,  GLShader(glsl, {tags}));
                }
                if(surface.name=="land"_) surface.shader.blendAlpha = true;

                // Loads textures
                int unit=1;
                for(const String& name: surface.shader.shader.sampler2D) {
                    surface.shader.shader[name] = unit++;
                    String file = existsFile(name+".png"_,folder) ? name+".png"_ : name+".jpg"_;
                    surface.shader<<GLTexture(decodeImage(readFile(file, folder)), SRGB|Mipmap|Bilinear|Anisotropic);
                }
            }
        }

        for(Model& model: models) {
            for(const mat4& instance : model.instances) {
                //if(scale>10) continue; // Ignores water plane
                for(Surface& surface : model.surfaces) {
                    for(Vertex& vertex: surface.vertices) { //TODO: smallest enclosing sphere
                        // Computes scene bounds in world space to fit view
                        vec3 position = instance*vertex.position;
                        worldMin=min(worldMin, position);
                        worldMax=max(worldMax, position);

                        // Compute scene bounds in light space to fit shadow
                        assert(sun!=mat4());
                        vec3 P = sun*position;
                        lightMin=min(lightMin,P);
                        lightMax=max(lightMax,P);
                    }
                }
            }
        }
        worldCenter = (worldMin+worldMax)/2.f; worldRadius=norm(worldMax.xy()-worldMin.xy())/2.f; //FIXME: compute smallest enclosing sphere
    }

    void render(int2 position, int2 size) override {
        // Render sun shadow map
        if(!sunShadow) {
            sunShadow = GLFrameBuffer(GLTexture(4096, 4096, Depth|Shadow|Bilinear|Clamp));
            sunShadow.bind(ClearDepth);

            // Normalizes to [-1,1]
            mat4 normalize;
            normalize.scale(vec3(1,1,-1)); // -> [-1,1]
            normalize.translate(-1); // -> [-1,1]
            normalize.scale(2.f/(lightMax-lightMin)); // -> [0,2]
            normalize.translate(-lightMin); // -> [0,max-min]
            sun = normalize * sun;

            shadow.bind();
            for(Model& model: models) {
                for(const Surface& surface: model.surfaces) {
                    surface.vertexBuffer.bindAttribute(shadow, "aPosition", 3, offsetof(Vertex,position));
                    for(const mat4& instance: model.instances) {
                        shadow["modelViewProjectionTransform"] = sun*instance;
                        surface.indexBuffer.draw();
                    }
                }
            }

            // Normalizes from [-1,1] to [0,1]
            mat4 sampler2D;
            sampler2D.scale(1./2);
            sampler2D.translate(1);
            sun = sampler2D * sun;
        }

        // Computes view transform
        uint width=size.x, height = size.y;
        mat4 projection, view;
#if PERSPECTIVE
        projection.perspective(2*atan(36,2*focalLength), width, height, 1./4, 4);
        view.scale(1.f/worldRadius); // fit scene (isometric approximation)
#else
        projection.scale(vec3(height*1./width,1,-1/viewScale));
        view.scale(viewScale/worldRadius); // fit scene (isometric approximation)
#endif
        view.translate(vec3(0,0,-worldRadius)); // step back
        view.rotateX(rotation.y); // pitch
        view.rotateZ(rotation.x); // yaw
        view.translate(vec3(0,0,-worldCenter.z));

        // Render impostors (TODO: 60fps limit, update impostors while idle between frames)
        //bake.bind(); bake.bindFragments({"NNNZ"_});
        for(Model& model: models) {
            if(model.instances.size<impostorMin) continue;
            array<Vertex7> vertices;
            array<uint> indices;
            if(!model.impostors.size<model.instances.size) model.impostors.grow(model.instances.size);
            for(uint i : range(model.instances.size)) {
                const mat4& instance = model.instances[i];
                // Computes view axis-aligned bounding box of object's oriented bounding box (FIXME: bounding sphere projection might be smaller)
                mat4 device; device.scale(vec3(width,height,1));
                mat4 transform = device*projection*view*instance;
                vec3 O = transform*vec3(0), min = O, max = O;
                for(int i: range(2)) for(int j: range(2)) for(int k: range(2)) {
                    vec3 corner = transform*vec3(i?-1:1,j?-1:1,k?-1:1);
                    min=::min(min, corner), max=::max(max, corner);
                }
                min.x=floor(min.x), min.y=floor(min.y);
                max.x=ceil(max.x), max.y=ceil(max.y);
                {mat4 normalize;
                    normalize.scale(vec3(1,1,255/(max.z-min.z))); // [0,size] in XY, [1,254] in Z
                    normalize.translate(-min); // -> [0,max-min] in XY;
                    transform = normalize * transform;
                }

                if(i==model.impostorIndex) {
                    int2 size = int3(max-min).xy();
                    if(!size.x || !size.y) continue;
                    if(size.x>512 || size.y>512) continue; //FIXME: direct render

                    //FIXME: VFC cull impostor updates
                    GLTexture& impostor = model.impostors[i];
                    mat3 normalMatrix = (view*instance).normalMatrix();

                    Image image(size.x, size.y, true);
                    clear(image.buffer.begin(), image.buffer.size, byte4(0,0,0xFF,0));

                    for(uint i: range(model.surfaces.size)) {
                        Surface& surface = model.surfaces[i];
                        for(uint i=0; i<surface.indices.size; i+=3) { // Rasterization, TODO: switch to raycast, faster with grid for high depth complexity
                            const Vertex& v1 = surface.vertices[surface.indices[i+0]];
                            const Vertex& v2 = surface.vertices[surface.indices[i+1]];
                            const Vertex& v3 = surface.vertices[surface.indices[i+2]];
                            vec3 p1 = transform*v1.position; vec2 a=p1.xy();
                            vec3 p2 = transform*v2.position; vec2 b=p2.xy();
                            vec3 p3 = transform*v3.position; vec2 c=p3.xy();
                            int2 min = int2(::min(::min(a,b),c)); // no need to clip to device as device is fitted to model
                            int2 max = int2(::max(::max(a,b),c));
                            for(uint y=min.y; y<=(uint)max.y; y++) {
                                for(uint x=min.x; x<=(uint)max.x; x++) {
                                    vec2 p = vec2(x,y);
                                    if(cross(p-a,b-a)<0) continue; // FIXME: indirect winding as we reverse Z in projection (FIXME: precompute edges)
                                    if(cross(p-b,c-b)<0) continue;
                                    if(cross(p-c,a-c)<0) continue;
                                    uint z = round((p1.z+p2.z+p3.z)/3); // Assumes triangle are small enough to spare interpolation
                                    assert_(x<image.width && y<image.height && z<0x100, (int)x, (int)y, (int)z);
                                    if(z>=image(x,y).r) continue; // Depth test (reversed Z in projection -> pass if less)
                                    vec3 n = round((v1.normal+v2.normal+v3.normal)/3.f); // Assumes triangle are small enough to spare interpolation
                                    n = normalize(normalMatrix*n);
                                    //if(n.z < 0) n=-n; Double sided ? If it is the case, FIXME: as we assume direct winding
                                    uint nx = 255*(n.x+1)/2, ny = 255*(n.y+1)/2;
                                    assert_(nx<0x100 && ny<0x100, n);
                                    image(x,y) = byte4(nx, ny, z, 1+i);
                                }
                            }
                        }
                        //FIXME: trim
                    }
                    impostor = GLTexture(image);
                }

                mat4 inverse = (device*projection*view).inverse();
                vec3 zVector = inverse*vec3(0,0,255); // Impostor unit vector in world space //FIXME: projection shifts it at each corner
                uint firstIndex = vertices.size;
                indices << firstIndex << firstIndex+1 << firstIndex+2 << firstIndex+1 << firstIndex+2 << firstIndex+3;
                vertices
                        << Vertex7{inverse*vec3(min.x, min.y, min.z),vec2(0,0),zVector}
                        << Vertex7{inverse*vec3(max.x,min.y, min.z),vec2(1,0),zVector}
                        << Vertex7{inverse*vec3(min.x, max.y,min.z),vec2(0,1),zVector}
                        << Vertex7{inverse*vec3(max.x,max.y,min.z),vec2(1,1),zVector};
            }
            model.impostorsIndices.upload(indices);
            model.impostorsVertices.upload(vertices); //FIXME: append impostor
            model.impostorIndex = (model.impostorIndex+1)%model.impostors.size;
        }

        if(framebuffer.size() != size) {
            framebuffer=GLFrameBuffer(width,height,-1);
            resolvedBuffer=GLTexture(width,height);
        }
        framebuffer.bind(ClearDepth);

        // Sky (TODO: fog)
        sky.bindFragments({"color"_});
        sky["inverseViewProjectionMatrix"_] = (projection*view).inverse();
        sky[sky.sampler2D.first()] = 0; skymap.bind(0);
        vertexBuffer.bindAttribute(sky,"position"_,2);
        vertexBuffer.draw(TriangleStrip);

        // World-space lighting
        vec3 sunLightDirection = normalize(sun.inverse().normalMatrix()*vec3(0,0,-1));

        for(Model& model: models) {
            if(model.impostors) {
                glDepthMask(0);
                glBlendAlpha();
                impostor.bind();
                impostor.bindFragments({"color"_});
                impostor["modelViewProjectionTransform"_] = projection*view;
                impostor["impostor"_] = 0;
                // Impostor normals are in view space (positive Z) FIXME: should be the view transform used to render the impostor
                impostor["lightDirection"_] = normalize(view.normalMatrix()*sunLightDirection);
                impostor["shadowMap"_] = 1; sunShadow.depthTexture.bind(1);
                impostor["shadowTransform"_] = sun;
                model.impostorsVertices.bindAttribute(impostor,"aPosition"_, 3, offsetof(Vertex7,position));
                model.impostorsVertices.bindAttribute(impostor,"aTexCoords"_, 2, offsetof(Vertex7,texcoords));
                //model.impostorsVertices.bindAttribute(impostor,"aZVector"_, 2, offsetof(Vertex7,zVector));
                for(uint i: range(model.impostors.size)) {
                    if(!model.impostors[i]) continue;
                    model.impostors[i].bind(0);
                    model.impostorsIndices.draw(i*6, 6);
                }
                glBlendNone();
                glDepthMask(1);
                continue;
            }
            for(Surface& surface: model.surfaces) {
                if(model.instances.size>impostorMin) continue; //FIXME
                if(surface.shader.blendAlpha) glBlendAlpha();
                GLShader& shader = surface.shader.shader;
                shader.bind();
                shader.bindFragments({"color"_});
                shader["lightDirection"_] = sunLightDirection;
                shader["shadowMap"_] = 0; sunShadow.depthTexture.bind(0);

                {int i=1; for(const String& name: shader.sampler2D) shader[name] = i++; }
                {int i=1; for(const GLTexture& texture : surface.shader) texture.bind(i++); }

                surface.vertexBuffer.bindAttribute(shader,"aPosition"_, 3,offsetof(Vertex,position));
                surface.vertexBuffer.bindAttribute(shader,"aNormal"_, 3,offsetof(Vertex,normal));

                for(const mat4& instance : model.instances) {
                    shader["modelViewProjectionTransform"_] = projection*view*instance;
                    shader["normalMatrix"_] = instance.normalMatrix();
                    shader["shadowTransform"_] = sun*instance;
                    surface.indexBuffer.draw();
                }
                if(surface.shader.blendAlpha) glBlendNone();
            }
        }

        framebuffer.blit(resolvedBuffer);
        GLFrameBuffer::bindWindow(int2(position.x,window.size.y-height-position.y), size, 0);
        present["framebuffer"_]=0; resolvedBuffer.bind(0);
        vertexBuffer.bindAttribute(present,"position"_,2);
        vertexBuffer.draw(TriangleStrip);

        frameCount++;
        const float alpha = 1./60; frameTime = (1-alpha)*frameTime + alpha*(float)time;
        String status = dec(round(frameTime*1000),3)+"ms "_+ftoa(1/frameTime,1,2)+"fps"_;
        //if(frameCount%32==0) statusChanged(status);
        if(isPowerOfTwo(frameCount) && frameCount>1) log(models.last().instances.size, dec(frameCount,4), status);
        time.reset();
        window.render();
    }

    // Orbital view control
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override {
        int2 delta = cursor-lastPos; lastPos=cursor;
        if(event==Motion && button==LeftButton) {
            rotation += float(2.f*PI)*vec2(delta)/vec2(size); //TODO: warp
            rotation.y= clip(float(-PI/2),rotation.y,float(0)); // Keep pitch between [-PI/2,0]
        }
#if PERSPECTIVE
        else if(event == Press && button == WheelUp) focalLength += zoomSpeed;
        else if(event == Press && button == WheelDown) focalLength = max(1.f,focalLength-zoomSpeed);
#else
        else if(event == Press && button == WheelUp) viewScale *= zoomSpeed;
        else if(event == Press && button == WheelDown) viewScale /= zoomSpeed;
#endif
        else return false;
        return true;
    }
} application;
