#include "thread.h"
#include "string.h"
#include "data.h"
#include "map.h"
#include "matrix.h"
#include "bsp.h"
#include "md3.h"

//TODO: restore gl.h
struct GLShader {};
string str(const GLShader&) { return "GLShader"_; }

struct Texture {
    Texture(const string& path=""_/*, string type="albedo"_*/) : path(path), type("albedo"_) {}
    void upload();

    String path;
    String type; //FIXME: convert to flags
    //const GLTexture* texture = 0;
    bool alpha = false;
    vec2 tcScale {1,1};    //vec3 tcScale {1,1,1/*.0/16*/};
    vec3 rgbScale {1,1,1};
    //string heightMap;
    //bool inverted = true;

    //static map<string,GLTexture> textures;
};

struct Shader : array<Texture> {
    Shader(string type="transform surface"_): name(type), type(type) {}
    //GLShader* bind();

    String name;
    String type;
    GLShader* program = 0;
    bool polygonOffset=false, alphaTest=false, blendAdd=false, blendAlpha=false, tangentSpace=false, vertexBlend=false;
    string file; int firstLine=0, lastLine=0;
    map<string,String> properties;
};

struct Vertex {
    vec3 position; vec2 texcoord; vec3 normal; vec3 tangent; vec3 bitangent; float alpha,ambient; // 16f=64B
    //Vertex() {}
    Vertex(vec3 position,vec2 texcoord,vec3 normal,float alpha=0):position(position),texcoord(texcoord),normal(normal),alpha(alpha) {}
};

struct Surface {
    /// Adds a vertex and updates bounding box
    /// \return Index in vertex array
    uint addVertex(Vertex v);
    /// Adds a triangle to the surface by copying new vertices as needed
    /// \note Assumes identical sourceVertices between all calls
    /// \note Handles tangent basis generation
    void addTriangle(const ref<Vertex>& sourceVertices, int i1, int i2, int i3);
    ///
    //void draw(GLShader* program,bool withTexcoord,bool withNormal,bool withAlpha,bool withTangent);
    ///
    //bool raycast(vec3 origin, vec3 direction, float& z);

    vec3 bbMin,bbMax;
    map<int,int> indexMap;
    array<Vertex> vertices;
    array<uint> indices;
    //GLBuffer buffer;
    const Shader* shader = 0;
};

uint Surface::addVertex(Vertex v) {
    if(!vertices) bbMin=bbMax=v.position; else bbMin=min(bbMin,v.position), bbMax=max(bbMax,v.position); // Initializes or updates bounding box
    vertices<<v; return vertices.size-1; // Appends vertex
}

void Surface::addTriangle(const ref<Vertex>& sourceVertices, int i1, int i2, int i3) {
    if(!indexMap.contains(i1)) indexMap[i1]=addVertex(sourceVertices[i1]);
    if(!indexMap.contains(i2)) indexMap[i2]=addVertex(sourceVertices[i2]);
    if(!indexMap.contains(i3)) indexMap[i3]=addVertex(sourceVertices[i2]);
    Vertex& v1 = vertices[indexMap[i1]];
    Vertex& v2 = vertices[indexMap[i2]];
    Vertex& v3 = vertices[indexMap[i3]];

    vec2 p[3]; for(int i: range(3)) p[i] = vec2(v2.position[i] - v1.position[i], v3.position[i] - v1.position[i]);
    vec2 s(v2.texcoord.x - v1.texcoord.x, v3.texcoord.x - v1.texcoord.x);
    vec2 t(v2.texcoord.y - v1.texcoord.y, v3.texcoord.y - v1.texcoord.y);
    float sign = cross(s,t)<0?-1:1;
    vec3 tangent(   cross(t,p[0]) * sign,  cross(t,p[1]) * sign,  cross(t,p[2]) * sign );
    vec3 bitangent(-cross(s,p[0]) * sign, -cross(s,p[1]) * sign, -cross(s,p[2]) * sign );

    v1.tangent += tangent, v2.tangent += tangent, v3.tangent += tangent;
    v1.bitangent += bitangent, v2.bitangent += bitangent, v3.bitangent += bitangent;
    indices << indexMap[i1] << indexMap[i2] << indexMap[i3];
}

struct Object {
    Object(const Surface& surface) : surface(surface) {}

    const Surface& surface;
    vec3 uniformColor {1,1,1};
    mat4 transform;
    vec3 center; vec3 extent; // Bounding box center and extent
    int planeIndex = 0; // Last hit bounding box plane index
};

struct Light {
    Light( vec3 origin, float radius, vec3 color, bool diffuse, bool specular, bool shadow) : origin(origin), radius(radius), color(color), diffuse(diffuse), specular(specular),shadow(shadow) {}
    //void project(const mat4& projection,const mat4& view);
    bool operator<(const Light& other) const { return -center.z < -other.center.z; }

    vec3 origin;
    float radius;
    vec3 color;
    bool diffuse,specular,shadow;
    vec3 clipMin,clipMax,center;
    int planeIndex = 0;
};

typedef map<string,string> Entity;

vec3 toVec3(const string& data) { TextData s(data); vec3 v; for(uint i: range(3)) { v[i]=s.decimal(); if(i<2) s.skip(" "_); } return v; }

struct ET {
    void parseMaterialFile(string path) {
        int line=0; //FIXME: ->TextData
        int comment=0;
        for(TextData s(readFile(path,data)); s; s.skip()) {
            s.skip();
            if(s.match("//"_)) { s.until('\n'); continue; }
            if(s.match("/*"_)) { comment++; continue; }
            if(s.match("*/"_)) { comment--; continue; }
            if(comment) continue;
            string name = s.identifier("/_"_);
            if(!name) error("Unexpected outside shader definition", s.line(), "in", path, s.slice(0,s.index));
            Shader& shader = shaders[name]; /*May already exist*/ shader.name=String(name); shader.clear(); shader.properties.clear(); shader.program=0;
            shader.file=path; shader.firstLine=line;
            int nest=0,comment=0; Texture* current=0; bool alphaBlend=false;
            while(s) {
                s.skip();
                if(s.match("//"_)) { s.until('\n'); continue; }
                if(s.match("/*"_)) { comment++; continue; }
                if(s.match("*/"_)) { comment--; continue; }
                if(comment) continue;
                if(s.match('{')) { nest++; continue; }
                if(s.match('}')) { nest--; if(nest==0) break; if(nest==1) current=0; continue; }
                string key = s.identifier("_"_); s.whileAny(" "_);
                string value = s.untilAny("\r\n"_);
                array<string> args = split(value); //l.replace(QRegExp("[,()]")," ").simplified().split(' ').mid(1);
                args.filter([](const string& s){return s=="("_||s==")"_;});
                /**/ if(key=="map"_) {
                    if(startsWith(args[0],"$"_)) { current=0; continue; } //TODO: lightmap
                    if(!current) { shader.append(Texture()); current=&shader.last(); }
                    current->path = String(args[0]);
                }
                else if(split("implicitMap implicitBlend implicitMask lightmap clampmap diffusemap"_).contains(key)) {
                    string map = args[0];
                    if(map=="-"_) map = name;
                    shader.append(Texture(map)); current=&shader.last();
                    if(key=="implicitMask"_||key=="implicitBlend"_) { current->alpha=true; current->type<<" alphaTest"_; shader.alphaTest=true; }
                }
                /*else if(key=="bumpmap"_||key=="normalmap") {
                        bool hasBumpMap=false; foreach(Texture t,shader) if(t.type.contains("tangent")) {
                            hasBumpMap=true;
                            warning("duplicate bumpMap stage");
                        }
                        if(hasBumpMap) continue;
                        Texture map(args[0],"tangent");
                        if(args[0]=="displacemap") {
                            map.path=args[1];
                            map.type+=" displace cone";
                            if(args[2]=="invertColor") {
                                map.heightMap=args[3];
                            } else {
                                map.inverted=false;
                                map.heightMap=args[2];
                            }
                        } else if(args[0]=="addnormals") {
                            map.path=args[1];
                            if(args[2]=="heightMap") {
                                map.type+=" displace cone";
                                map.heightMap=args[3];
                            }
                        } else {
                            if(args[0]=="_flat") { warning("normalMap _flat is a noop"); continue; }
                            if(args.value(1)=="heightMap") {
                                map.path=""; //TODO: compute normal from height
                                map.type+=" displace cone";
                                map.heightMap=args[2];
                            }
                        }
                        shader.prepend(map); shader.tangentSpace=true;
                    } else ifMatch("specularmap") {
                        shader.append(Texture(args[0],"specular")); current=&shader.last();
                    } else ifMatch("stage") {
                        if(!current) { shader.append(Texture()); current=&shader.last(); }
                        if(args[0]=="diffusemap") current->type="albedo";
                        else if(args[0]=="normalmap"||args[0]=="bumpmap") current->type="tangent";
                    }*/
                else if(key=="animMap"_) { shader.append(Texture(args[1])); current=&shader.last(); }
                //else ifMatch2("cull","none disable twosided") shader.doubleSided=true; //split("a b").contains(args[0])
                else if(key=="polygonOffset"_ || key=="polygonoffset"_) shader.polygonOffset=true;
                else if(key=="surfaceParm"_||key=="surfaceparm"_) { if(args[0]=="trans"_||args[0]=="alphashadow"_) alphaBlend=true; }
                else if(key=="sort"_) { if(key=="additive"_) alphaBlend=true; }
                else if(key=="skyparms"_) { shader.clear(); shader.properties["skyparms"_]=String(value); }
                else if(key=="fogparms"_) { shader.properties.insert("fogparms"_, String(args[3])); } //FIXME: fog color
                else if(key=="q3map_sun"_||key=="q3map_sunExt"_) shader.properties["q3map_sun"_]=String(value);
                else if(split("nocompress nopicmip nomipmaps nofog fog waterfogvars cull tcGen tcgen depthWrite depthFunc detail qer_editorimage qer_trans q3map_globaltexture q3map_lightimage q3map_surfacelight q3map_clipModel q3map_shadeangle"_).contains(key)) {} // Ignored or default
                else if(!current) { /*error("No current texture for",key,args); Happens on explicit $lightmap blendFunc multiply*/ continue; }
                else if(key=="alphaFunc"_||key=="alphafunc"_) { current->alpha=true; current->type<<" alphaTest"_; shader.alphaTest=true; }
                else if(key=="vertexColor"_) { current->type<<" vertexAlpha"_; shader.vertexBlend=true; }
                else if(key=="alphaGen"_||key=="alphagen"_) { if(args[0]=="vertex"_) { current->type<<" vertexAlpha"_; shader.vertexBlend=true; } }
                else if(key=="tcMod"_||key=="tcmod"_) {
                    if(args[0]=="scale"_) current->tcScale=vec3(toDecimal(args[1]),toDecimal(args[2]),1).xy();
                    //TODO: rotate
                }
                else if(key=="rgbGen"_||key=="rgbgen"_) {
                    if(args[0]=="const"_) current->rgbScale=vec3(toDecimal(args[1]),toDecimal(args[2]),toDecimal(args[3]));
                    if(args[0]=="wave"_) current->rgbScale=vec3(toDecimal(args[2]),toDecimal(args[3]),toDecimal(args[4]));
                    //else
                }
                else if(key=="blend"_||key=="blendFunc"_||key=="blendfunc"_||key=="blenfunc"_) {
                    if(shader.size==1&&!alphaBlend) continue;
                    string sfactor,dfactor; //FIXME: flags
                    if(args[0]=="add"_) { sfactor="one"_; dfactor="one"_; }
                    else if(args[0]=="filter"_) { sfactor="dst_color"_; dfactor="zero"_; }
                    else if(args[0]=="detail"_) { sfactor="dst_color"_; dfactor="src_color"_; }
                    else if(args[0]=="decal"_) { sfactor="dst_color"_; dfactor="src_color"_; }
                    else if(args[0]=="blend"_) { sfactor="src_alpha"_; dfactor="one_minus_src_alpha"_; }
                    else { assert(args.size==2, args, "'"_+args[0]+"'"_, hex(args[0]), args[0]=="blend"_); sfactor=args[0].slice(3); dfactor=args[1].slice(3);
                        if(toLower(sfactor)=="zero"_ && toLower(dfactor)=="src_color"_) { sfactor="dst_color"_; dfactor="zero"_; }
                    }
                    if(sfactor=="one"_ && dfactor=="zero"_) continue;
                    if(alphaBlend) { shader.blendAlpha=true;
                        if(!find(shader.type,"fog"_)) shader.type<<" position fog"_; current->type<<" fog"_;
                        if(sfactor=="one"_ && dfactor=="one"_) { shader.blendAdd=true; continue; }
                        if((sfactor=="src_alpha"_&&dfactor=="one_minus_src_alpha"_)||(sfactor=="dst_color"_&&dfactor=="zero"_)) {
                            current->alpha=true; current->type<<" alphaBlend"_; continue;
                        }
                    }
                    current->type+=" sfactor_"_+toLower(sfactor)+" dfactor_"_+toLower(dfactor);
                }
                else error("Unknown key",key, args);
            }
            //if(!shader) error("Empty shader",name, s.slice(0,s.index)); //Happens on fog shader
            if(shader.size>8) error("Too many textures for shader",name,"in",path);
            shader.lastLine=line;
        }
    }

    array<String> search(const string& query, const string& type) {
        array<String> files;
        string folder = section(query,'/',0,-2);
        string fileName = section(query,'/',-2,-1);
        if(existsFolder(folder,data)) for(const String& file: Folder(folder, data).list(Files)) if(startsWith(file, fileName) && endsWith(file, type)) files << folder+"/"_+file;
        return files;
    }

    array<Surface> importBSP(const BSP& bsp, const ref<Vertex>& vertices, int firstFace, int numFaces, bool leaf) {
        map<int, Surface> surfaces;
        for(int f: range(firstFace,firstFace+numFaces)) {
            const bspFace& face = bsp.faces()[leaf?bsp.leafFaces()[f]:f];
            Surface& surface = surfaces[face.texture];
            if(face.type==1||face.type==3) {
                for(int i=0;i<face.numIndices;i+=3) {
                    surface.addTriangle(vertices,
                                         face.firstVertex+bsp.indices()[face.firstIndex+i+2],
                                         face.firstVertex+bsp.indices()[face.firstIndex+i+1],
                                         face.firstVertex+bsp.indices()[face.firstIndex+i+0]);
                }
            } else if(face.numIndices) error("Unsupported face.type",face.type,face.numIndices);
        }

        for(pair<int, Surface> surface: surfaces) {
            string name = strz(bsp.shaders()[surface.key].name);
            unique<Shader>& shader = shaders[name];
            //if(name.contains(QRegExp("ocean")/*|water|icelake*/)){shader.name=name; model<<Object(surfaces[i],shader); continue; }
            /*if(shader.properties.contains("skyparms")) { /// remove sky surfaces, parse sky params
                QString q3map_sun = shader.properties.value("q3map_sun");
                if(!q3map_sun.isEmpty()) {
                    if(!sky) sky = new Sky;
                    vec3 angles = vec3(q3map_sun.section(" ",4))*PI/180;
                    sky.sunDirection = vec3(cos(angles.x)*cos(angles.y),sin(angles.x)*cos(angles.y),sin(angles.y));
                    sky.sunIntensity=q3map_sun.section(" ",3,3).toFloat()/100;
                    Shader fog = shaders.value(entities["worldspawn"]["_fog"]);
                    sky.fogOpacity = fog.properties.value("fogparms","16384").toFloat();
                }
                continue;
            }*/
            if(!shader) { shader->name=String(name); shader->append(Texture(name)); }
            surface.value.shader = shader.pointer;
        }
        return move(surfaces.values);
    }

    array<Surface> importMD3(string modelPath) {
        Map map = Map(modelPath,data);
        const MD3& md3 = *(const MD3*)map.data.pointer;
        assert(ref<byte>(md3.magic)=="IDP3"_ && md3.version==15, modelPath);
        array<Surface> surfaces (md3.surfaceCount);
        for(uint offset=md3.surfaceOffset; offset!=md3.endOffset; offset+=md3.surface(offset).endOffset) {
            const md3Surface& surface = md3.surface(offset);
            assert(ref<byte>(surface.magic)=="IDP3"_, surface.magic);
            buffer<Vertex> vertices (surface.vertexCount);
            for(uint i : range(surface.vertices().size)) {
                md3Vertex v = surface.vertices()[i];
                vec2 textureCoordinates = surface.textureCoordinates()[i];
                float latitude = v.latitude*(2*PI/255), longitude = v.longitude*(2*PI/255);
                vertices[i] = Vertex(
                            vec3(v.position[0]/64.f,v.position[1]/64.f,v.position[2]/64.f),
                            vec2(textureCoordinates.x, 1.f-textureCoordinates.y /*Convert from upper left origin to OpenGL right handed (lower left)*/),
                            vec3(cos(longitude)*sin(latitude),sin(longitude)*sin(latitude),cos(latitude)) );
            }
            Surface target;
            for(const md3Triangle& face: surface.triangles()) target.addTriangle(vertices, face[0], face[1], face[2] );

            string shaderName;
            if(surface.shaders().size!=1) error("Multiple shader unsupported");
            for(const md3Shader& shader : surface.shaders()) {
                string name = strz(shader.name); //if(name.contains('.')) name=name.section('.',0,-2);
                String path = name.contains('/') ? String(name) : section(modelPath,'/',0,-2)+"/"_+name;
                string skin = name; //name.section("[",2).remove(']');
                for(string path : search(section(modelPath,'/',0,-2),".skin"_)) {
                    TextData s = readFile(path,data);
                    while(s) {
                        array<string> entry = split(trim(s.line()),',');
                        if(entry.size>=2 && entry[0]==skin) {
                            if(entry[1].contains('.')) entry[1]=section(entry[1],'.',0,-2);
                            shaderName = entry[1];
                            goto found; //break 2;
                        }
                    }
                }
                found:
                if(shaders.contains(shaderName)) target.shader = shaders.at(shaderName).pointer;
                else { Shader* shader = shaders[copy(path)].pointer; if(!shader) { shader->name=copy(path); shader->append(Texture(path)); } target.shader=shader; }
                break;
            }
            surfaces << move(target);
        }
        return surfaces;
    }

    ET() {
        /// Parse shader scripts
        array<String> materials = search("materials/"_,".mtr"_);
        if(!materials) materials = search("scripts/"_,".shader"_);
        if(!materials) error("No materials/*.mtr nor scripts/*.shader material shader script files found");
        for(string path: materials) parseMaterialFile(path);

        /// Loads BSP scene
        Map map = Map(data.list(Files|Recursive).filter([](const string& file){return !endsWith(file,".bsp"_);}).first(), data);
        const BSP& bsp = *(const BSP*)map.data.pointer;
        assert(ref<byte>(bsp.magic)=="IBSP"_ && bsp.version==47);

        /// BSP Entities
        for(TextData s(bsp.entities()); s;) {
            s.skip();
            if(s.match("{"_)) {
                Entity entity;
                while(s && (s.skip(), !s.match("}"_))) {
                    s.skip("\""_); string key = s.until("\""_);
                    s.skip(); s.skip("\""_); string value = s.until("\""_);
                    entity.insert(key,value);
                }
                if(entity.contains("targetname"_)) targets.insertMulti(entity.at("targetname"_), copy(entity)); //move?
                entities.insertMulti(entity.at("classname"_), move(entity));
            } else if(s.match('\0')) break;
            else error("Expected '{'"_, s.line());
        }
        entities.at("worldspawn"_).insert("model"_, "*0"_);

        /// BSP Vertices
        buffer<Vertex> vertices (bsp.vertices().size);
        for(uint i: range(bsp.vertices().size)) { const ibspVertex& v = bsp.vertices()[i]; vertices[i] = Vertex(v.position, v.texture, v.normal, v.color[3]); }

        /// BSP Faces
        for(const bspLeaf& leaf: bsp.leaves()) models["*"_+str(0)]<< importBSP(bsp, vertices, leaf.firstFace, leaf.numFaces, true);
        for(uint i: range(bsp.models().size)) {
            const bspModel& model = bsp.models()[i];
            models["*"_+str(i)]<< importBSP(bsp, vertices, model.firstFace, model.numFaces, false);
        }

        /// Convert Entities to Objects
        for(const Entity& e: entities.values) {
            //if(e.contains("target")) transform.translate(vec3(targets[e["target"]]["origin"]));
            if(e.contains("model"_)) {
                string name = e.at("model"_);
                if(!models.contains(name) && !startsWith(name,"*"_)) {
                    array<String> paths = search(section(name,'.',0,-2)+"."_,"md3"_);
                    //for(string type: ref<string>{"md3"_}) {
                        for(string path : paths) {
                            /*if(!endsWith(path, type)) continue;
                            if(endsWith(name,".md3"_))*/ { models[name]=importMD3(path); break /*2*/; }
                        }
                    //}
                }
                if(!models.contains(name)) { error(name); continue; }
                if(!models.at(name)) continue; // Some BSP models are empty
                mat4 transform;
                transform.translate(toVec3(e.value("origin"_,"0 0 0"_)));
                transform.scale(toDecimal(e.value("modelscale"_,"1"_)));
                transform.rotateZ(toDecimal(e.value("angle"_,"0"_))*PI/180);
                for(Object object: models.at(name)) {
                    object.transform = transform;
                    vec3 A = transform * object.surface.bbMin, B = transform * object.surface.bbMax;
                    // Computes world axis-aligned bounding box of object's oriented bounding box
                    vec3 O = 1.f/2*(A+B), min = O, max = O;
                    for(int i: range(1)) for(int j: range(1)) for(int k: range(1)) {
                        vec3 corner = transform*vec3((i?A:B).x,(j?A:B).y,(k?A:B).z);
                        min=::min(min, corner), max=::max(max, transform*corner);
                    }
                    object.center = 1.f/2*(min+max), object.extent = 1.f/2*abs(max-min);
                    /*if(object.shader->name.contains(QRegExp("ocean"))) { //|water|icelake
                        if(!water) water = new Water();
                        water->surfaces<<object;
                        water->z=max(water->z,(transform*object.surface->bbMax).z);
                        continue;
                    }*/
                    //object.shader->bind(); //force shader compilation
                    const Shader* shader = object.surface.shader;
                    GLShader* id = shader->program;
                    if(shader->name=="textures/common/caulk"_) shadowOnly[id] << object;
                    else if(shader->blendAdd) blendAdd[id] << object;
                    else if(shader->blendAlpha) blendAlpha[id] << object;
                    else if(shader->alphaTest) alphaTest[id] << object;
                    else opaque[id] << object;
                }
            }
            if(e.at("classname"_)=="light"_) {
                lights << Light(toVec3(e.at("origin"_)),
                                toVec3(e.value("light_radius"_,"300"_)).x,
                                toVec3(e.value("_color"_,"1 1 1"_)),
                                e.value("nodiffuse"_)!="1"_,
                                e.value("nospecular"_)!="1"_,
                                e.value("noshadows"_)!="1"_);
            }
        }
    }

    Folder data = "opt/enemy-territory/etmain"_;

    map<string,Entity> entities;
    map<string,Entity> targets;
    map<String,unique<Shader>> shaders; // Referenced by Surfaces
    map<String, array<Surface>> models;
    map<GLShader*,array<Object>> opaque, alphaTest, blendAdd, blendAlpha, shadowOnly; // Objects splitted by renderer state and indexed by GL Shader (to minimize context switches)
    array<Light> lights;
} et;
