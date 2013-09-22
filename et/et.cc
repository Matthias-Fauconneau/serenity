#include "thread.h"
#include "string.h"
#include "data.h"
#include "map.h"
#include "bsp.h"

//TODO: restore gl.h
struct GLShader {};

struct Vertex {
    vec3 position; vec2 texcoord; vec3 normal; vec3 tangent; vec3 bitangent; float alpha,ambient; // 16f=64B
    Vertex() {}
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

struct Texture {
    Texture(string path/*=""_*/, string type="albedo"_) : path(path), type(type) {}
    void upload();

    string path;
    string type;
    //const GLTexture* texture = 0;
    //bool alpha = false;
    //vec3 tcScale {1,1,1/*.0/16*/};
    //vec3 rgbScale {1,1,1};
    //string heightMap;
    //bool inverted = true;

    //static map<string,GLTexture> textures;
};

struct Shader : array<Texture> {
    Shader(string type="transform surface"_): name(type), type(type) {}
    //GLShader* bind();

    string name;
    string type;
    GLShader* program = 0;
    bool polygonOffset=false, alphaTest=false, blendAdd=false, blendAlpha=false, tangentSpace=false, vertexBlend=false;
    string file; int firstLine=0, lastLine=0;
    map<string,string> properties;
};


struct Object {
    Object(const Surface& surface, const Shader& shader) : surface(surface), shader(shader) {}

    const Surface& surface;
    const Shader& shader;
    vec3 uniformColor {1,1,1};
    //mat4 transform;
    vec3 center; vec3 extent; int planeIndex=0;
};

typedef map<string,string> Entity;

vec3 toVec3(const string& data) { TextData s(data); vec3 v; for(uint i: range(3)) { v[i]=s.decimal(); if(i<3) s.skip(" "_); return v; } }

struct ET {
    Folder data = "opt/enemy-territory/etmain"_;

    map<string,Entity> entities;
    map<string,Entity> targets;
    map<string,unique<Shader>> shaders; // Referenced by Objects
    map<String, array<Object>> models;
    map<GLShader*,array<Object>> opaque, alphaTest, blendAdd, blendAlpha, shadowOnly; // Objects splitted by renderer state and indexed by GL Shader (to minimize context switches)

    array<Object> bspImport(const BSP& bsp, const ref<Vertex>& vertices, int firstFace, int numFaces, bool leaf) {
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

        array<Object> model;
        for(int i: surfaces.keys) {
            string name = strz(bsp.shaders()[i].name);
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
            if(!shader) { shader->name=name; shader->append(Texture(name)); }
            model << Object(surfaces[i],shader);
        }
        return model;
    }

    ET() {
        //for(const String& file: data.list(Files|Recursive)) if(endsWith(file,".bsp"_)) log(file, File(file, data).size()/1024/1024.0);
        Map map = Map(data.list(Files|Recursive).filter([](const string& file){return !endsWith(file,".bsp"_);}).first(), data);
        const BSP& bsp = *(const BSP*)map.data.pointer;
        assert_(ref<byte>(bsp.magic)=="IBSP"_ && bsp.version==47);
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
        //for(const bspLeaf& leaf: bsp.leaves()) models.insert("*0"_, bspImport(bsp, vertices, leaf.firstFace, leaf.numFaces, true));
        for(uint i: range(bsp.models().size)) {
            const bspModel& model = bsp.models()[i];
            models.insert("*"_+str(i), bspImport(bsp, vertices, model.firstFace, model.numFaces, false));
        }

        /// Convert Entities to Objects
        for(const Entity& e: entities.values) {
            //if(e.contains("target")) transform.translate(vec3(targets[e["target"]]["origin"]));
            vec3 origin = toVec3(e.at("origin"_));
            if(e.contains("model"_)) {
                float scale = toDecimal(e.value("modelscale"_,"1.0"_));
                float angle = toDecimal(e.value("angle"_,"0"_))*PI/180;

                string name = e["model"];
                #ifdef HAS_ASSIMP
                if(!models.contains(name) && !name.startsWith("*")) models[name]=assimpImport(name);
                #else
                if(!models.contains(name) && !name.startsWith("*")) error(name);
                #endif
                if(!models.contains(name)) continue;
                //if(models[name]->count()==0) qWarning()<<"Empty model "<<name;
                mat4 transform; transform.translate(origin); transform.scale(scale); transform.rotateZ(angle);
                foreach(Object object, models[name]) {
                    object.transform=transform;
                    vec3 A=transform*object.surface->bbMin,B=transform*object.surface->bbMax;
                    object.center=(A+B)/2; object.extent=abs(B-A)/2;
                    /*if(object.shader->name.contains(QRegExp("ocean"))) { //|water|icelake
                        if(!water) water = new Water();
                        water->surfaces<<object;
                        water->z=max(water->z,(transform*object.surface->bbMax).z);
                        continue;
                    }*/
                    object.shader->bind(); //force shader compilation
                    GLShader* id = object.shader->program;
                    objects << object;
                    (object.shader->name=="textures/common/caulk" ? shadowOnly[id] :
                    (object.shader->blendAdd ? blendAdd[id] :
                    (object.shader->blendAlpha ? blendAlpha[id] :
                    (object.shader->alphaTest ? alphaTest[id] : opaque[id])))) << &objects.last();
                }
            }
            if(e.at("classname")=="light") {
                lights << Light(origin,vec3(e.value("light_radius","300")).x,vec3(e.value("_color","1 1 1")),
                                e["nodiffuse"]!="1",e["nospecular"]!="1",e["noshadows"]!="1");
            }
        }
        //qDebug()<<entities.count()<<objects.count()<<lights.count();
    }
} et;
