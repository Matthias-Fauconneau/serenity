#include "scene.h"
#include "bsp.h"
#include "md3.h"

vec3 toVec3(const string& data) { TextData s(data); vec3 v; for(uint i: range(3)) { v[i]=s.decimal(); if(i<2) s.skip(" "_); } return v; }

void Scene::parseMaterialFile(string path) {
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
        shader.file=String(path); shader.firstLine=line; uint shaderFirstLineIndex = s.index;
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
            /**/ if(key=="map"_ || key=="clampmap"_ || key=="lightmap"_) { //diffusemap
                if(!current) { shader.append(Texture()); current=&shader.last(); }
                current->path = String(args[0]);
                if(key=="clampmap"_) current->clamp=true;
                if(key=="lightmap"_) assert_(current->path=="$lightmap"_);
            }
            else if(split("implicitMap implicitBlend implicitMask"_).contains(key)) {
                string map = args[0];
                if(map=="-"_) map = name;
                shader.append(Texture(map)); current=&shader.last();
                if(key=="implicitMask"_||key=="implicitBlend"_) { current->alpha=true; current->type<<" alphaTest"_; shader.alphaTest=true; }
                shader.append(Texture("$lightmap"_));
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
                            map.type<<" displace cone";
                            if(args[2]=="invertColor") {
                                map.heightMap=args[3];
                            } else {
                                map.inverted=false;
                                map.heightMap=args[2];
                            }
                        } else if(args[0]=="addnormals") {
                            map.path=args[1];
                            if(args[2]=="heightMap") {
                                map.type<<" displace cone";
                                map.heightMap=args[3];
                            }
                        } else {
                            if(args[0]=="_flat") { warning("normalMap _flat is a noop"); continue; }
                            if(args.value(1)=="heightMap") {
                                map.path=""; //TODO: compute normal from height
                                map.type<<" displace cone";
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
            else if(key=="skyparms"_) {
                //assert_(args[0]!="-"_, path, name);
                //for(shader.skyBox = Texture(args[0]); //TODO: asserts uniform color box #4D505B
                shader.cloudHeight = toDecimal(args[1]);
                assert_(args[2]=="-"_);
            }
            else if(key=="fogparms"_) { shader.properties.insert("fogparms"_, String(args[3])); } //FIXME: fog color
            else if(key=="q3map_sun"_||key=="q3map_sunExt"_) shader.properties["q3map_sun"_]=String(value);
            else if(split("nocompress nopicmip nomipmaps nofog fog waterfogvars cull tcGen tcgen depthWrite depthwrite depthFunc detail qer_editorimage qer_trans q3map_globaltexture q3map_lightimage q3map_surfacelight q3map_clipModel q3map_shadeangle"_).contains(key)) {} // Ignored or default
            else if(!current) { /*error("No current texture for",key,args); Happens on explicit $lightmap blendFunc multiply*/ continue; }
            else if(key=="alphaFunc"_||key=="alphafunc"_) { current->alpha=true; current->type<<" alphaTest"_; shader.alphaTest=true; }
            else if(key=="vertexColor"_) { current->type<<" vertexAlpha"_; shader.vertexBlend=true; }
            else if(key=="alphaGen"_||key=="alphagen"_) { if(args[0]=="vertex"_) { current->type<<" vertexAlpha"_; shader.vertexBlend=true; } }
            else if(key=="tcMod"_||key=="tcmod"_) {
                /**/ if(args[0]=="scale"_ || args[0]=="Scale"_) {
                    //current->tcMod = mat3x2(toDecimal(args[1]),0, 0,toDecimal(args[2]), 0,0) * current->tcMod;
                }
                else if(args[0]=="transform"_) {
                    array<double> m = apply(args.slice(1),toDecimal); // m00 m01 m10 m11 dx dy
                    current->tcMod = mat3x2(m[0],m[1], m[2],m[3], m[4],m[5]) * current->tcMod;
                }
                else if(args[0]=="rotate"_) {/*TODO*/}
                else if(args[0]=="scroll"_) {/*TODO*/}
                else if(args[0]=="stretch"_) {/*TODO*/}
                else if(args[0]=="turb"_) {/*TODO*/}
                else error(key, args);
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
                current->type << " sfactor_"_+toLower(sfactor)+" dfactor_"_+toLower(dfactor);
            }
            else error("Unknown key",key, args);
        }
        //if(!shader) error("Empty shader",name, s.slice(0,s.index)); //Happens on fog shader
        if(shader.size>8) error("Too many textures for shader",name,"in",path);
        shader.lastLine=line;
        shader.source=String(s.slice(shaderFirstLineIndex,s.index-shaderFirstLineIndex));
    }
}

array<String> Scene::search(const string& query, const string& type) {
    array<String> files;
    string folder = section(query,'/',0,-2);
    string fileName = section(query,'/',-2,-1);
    if(existsFolder(folder,data)) for(const String& file: Folder(folder, data).list(Files)) if(startsWith(file, fileName) && endsWith(file, type)) files << folder+"/"_+file;
    return files;
}

struct ID { int texture, lightmap; };
bool operator==(const ID& a, const ID& b) { return a.texture==b.texture && a.lightmap==b.lightmap; }
string str(const ID& o) { return str(o.texture, o.lightmap); }

Vertex bezier(const Vertex& a, const Vertex& b, const Vertex& c, float t) {
    size_t N = sizeof(Vertex)/sizeof(float);
    const float* A = (const float*)&a; const float* B = (const float*)&b; const float* C = (const float*)&c; float V[N];
    for(int i: range(N)) V[i] = (1-t)*(1-t)*A[i] + 2*(1-t)*t*B[i] + t*t*C[i];
    return (Vertex)(*(Vertex*)V);
}

Shader& Scene::getShader(string name, int lightmap) {
    Shader& shader = shaders[name];
    if(!shader.name) {
        shader.name=String(name);
        shader.append(Texture(name));
        shader.append(Texture("$lightmap"_));
    }
    for(Texture& texture: shader) {
        if(texture.path=="$lightmap"_ /*|| texture.path=="$lightgrid0"_*/) {
            if(lightmap>=0) {
                String lightMapID = "/lm_"_+dec(lightmap,4);
                Shader& lightMapShader = shaders[name+lightMapID];
                if(!lightMapShader.name) {
                    lightMapShader = copy(shader);
                    if(lightMapShader.last().path=="$lightgrid1"_) lightMapShader.pop();
                    assert(lightMapShader.size==shader.size);
                    for(Texture& texture: lightMapShader) if(texture.path=="$lightmap"_) {
                        texture.type = String("lightmap"_);
                        texture.path = this->name+lightMapID; // Assumes extern "high resolution" lightmaps are always used (TODO: intern lightmaps)
                    }
                    {int albedo=0; for(const Texture& tex: lightMapShader) albedo += find(tex.type,"albedo"_); assert_(albedo, lightMapShader);}
                }
                return lightMapShader;
            } else if(texture.path=="$lightmap"_) texture.type = String("lightgrid"_);
        }
    }
    return shader;
}

array<Surface> Scene::importBSP(const BSP& bsp, const ref<Vertex>& vertices, int firstFace, int numFaces, bool leaf) {
    map<ID, Surface> surfaces;
    for(int f: range(firstFace,firstFace+numFaces)) {
        const bspFace& face = bsp.faces()[leaf?bsp.leafFaces()[f]:f];
        string name = str(bsp.shaders()[face.texture].name);
        Shader& shader = shaders[name];
        if(shader.cloudHeight) { if(sky) assert_(sky==&shader); else sky=&shader; continue; }
        Surface& surface = surfaces[ID{face.texture, face.lightMapIndex}]; // Ensures surfaces are split by textures/lightmaps changes
        if(!surface.shader) surface.shader = &getShader(name, face.lightMapIndex);
        if(face.type==1||face.type==3) {
            for(int i=0;i<face.numIndices;i+=3) {
                surface.addTriangle(vertices,
                                    face.firstVertex+bsp.indices()[face.firstIndex+i+2],
                        face.firstVertex+bsp.indices()[face.firstIndex+i+1],
                        face.firstVertex+bsp.indices()[face.firstIndex+i+0]);
            }
        } else if(face.type==2) {
            int w = face.size[0], h = face.size[1];
            assert(w%2==1 && h%2==1 && w*h==face.numVertices && face.numIndices==0);
            for(int y=0;y<h-2;y+=2) for(int x=0;x<w-2;x+=2) {
                const int level=4;
                ref<Vertex> control = vertices.slice(face.firstVertex+y*w+x,w*h);
                static array<Vertex> interpolatedVertices; int firstIndex = interpolatedVertices.size; // HACK: Surface::addTriangle expects same array between calls
                for(int j: range(level+1)) for(int k: range(level+1)) {
                    float u = float(j)/float(level), v = float(k)/float(level);
                    interpolatedVertices << bezier(bezier(control[0*w+0],control[0*w+1],control[0*w+2],u),
                            bezier(control[1*w+0],control[1*w+1],control[1*w+2], u),
                            bezier(control[2*w+0],control[2*w+1],control[2*w+2], u), v);
                }
                for(int j: range(level)) for(int k: range(level)) {
                    int i = firstIndex + j*(level+1) + k;
                    surface.addTriangle(interpolatedVertices, i+(level+1), i+1, i+0 );
                    surface.addTriangle(interpolatedVertices, i+(level+1)+1, i+1, i+(level+1) );
                }
            }
        } else error("Unsupported face.type",face.type,face.numIndices);
    }

    return move(surfaces.values);
}

array<Surface> Scene::importMD3(string modelPath) {
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
        for(const md3Triangle& face: surface.triangles()) target.addTriangle(vertices, face[2], face[1], face[0] );

        String name = String(str(surface.shaders().first().name));
        if(surface.shaders().size!=1) error("Multiple shader unsupported");
        for(string path : search(section(modelPath,'/',0,-2)+"/"_,".skin"_)) {
            TextData s = readFile(path,data);
            while(s) {
                array<string> entry = split(trim(s.line()),',');
                if(entry.size>=2 && entry[0]==str(surface.name)) {
                    if(entry[1].contains('.')) entry[1]=section(entry[1],'.',0,-2);
                    name = String(entry[1]);
                    break;
                }
            }
        }
        target.shader = &getShader(name);
        surfaces << move(target);
    }
    return surfaces;
}

Scene::Scene(string file, const Folder& data) {
    ::data = Folder("."_, data);
    assert(endsWith(file,".bsp"_));
    name = String(section(file,'.',0,-2));

    /// Loads BSP scene
    Map map = Map(file, data);
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
                entity.insert(String(key),String(value));
            }
            if(entity.contains("targetname"_)) targets.insertMulti(copy(entity.at("targetname"_)), copy(entity)); //move?
            entities.insertMulti(copy(entity.at("classname"_)), move(entity));
        } else if(s.match('\0')) break;
        else error("Expected '{'"_, s.line());
    }
    entities.at("worldspawn"_).insert(String("model"_), String("*0"_));

    /// Loads lights (before parsing shaders to activate lightmaps if lights are not included in the BSP)
    for(const Entity& e: entities.values) {
        if(e.at("classname"_)=="light"_) {
            lights << Light(toVec3(e.at("origin"_)),
                            toVec3(e.value("light_radius"_,"300"_)).x,
                            toVec3(e.value("_color"_,"1 1 1"_)),
                            e.value("nodiffuse"_)!="1"_,
                            e.value("nospecular"_)!="1"_,
                            e.value("noshadows"_)!="1"_);
        }
    }

    /// Parses shader scripts
    array<String> materials = search("materials/"_,".mtr"_);
    if(!materials) materials = search("scripts/"_,".shader"_);
    if(!materials) error("No materials/*.mtr nor scripts/*.shader material shader script files found");
    for(string path: materials) parseMaterialFile(path);

    /// Loads BSP geometry
    buffer<Vertex> vertices (bsp.vertices().size);
    for(uint i: range(bsp.vertices().size)) {
        const ibspVertex& v = bsp.vertices()[i];
        //assert(v.color[0]==0xFF && v.color[1]==0xFF && v.color[2]==0xFF, v.color[0], v.color[1], v.color[2]);
        vertices[i] = Vertex(v.position, vec2(v.texture.x,/*1-*/v.texture.y), v.normal, v.color[3]/255.f, vec2(v.lightmap.x,/*1-*/v.lightmap.y),vec3(v.color[0]/255.f,v.color[1]/255.f,v.color[2]/255.f));
    }
    for(const bspLeaf& leaf: bsp.leaves()) models["*"_+str(0)]<< importBSP(bsp, vertices, leaf.firstFace, leaf.numFaces, true);
    for(uint i: range(bsp.models().size)) {
        const bspModel& model = bsp.models()[i];
        models["*"_+str(i)]<< importBSP(bsp, vertices, model.firstFace, model.numFaces, false);
    }


    if(sky) { /// Generates skybox model
        array<Vertex> vertices;
        Surface surface;
        for(int i = 0; i < 5; i++) { //FIXME: implement Surface::addTriangle without index indirection
            const int SKY_SUBDIVISIONS = 8;
            float MIN_T = i==4 ? -(SKY_SUBDIVISIONS/2) : -1;
            uint firstIndex = vertices.size;

            for(int t = MIN_T + (SKY_SUBDIVISIONS/2); t <= SKY_SUBDIVISIONS; t++) { // Iterates through the subdivisions
                for(int s = 0; s <= SKY_SUBDIVISIONS; s++) { // Compute vector from view origin to sky side integral point
                    vec3 skyVec;
                    float boxSize = 32768 / sqrt(3.); // zFar
                    vec3 b(((s - (SKY_SUBDIVISIONS/2)) / ( float ) (SKY_SUBDIVISIONS/2)) * boxSize, ((t - (SKY_SUBDIVISIONS/2)) / ( float ) (SKY_SUBDIVISIONS/2)) * boxSize, boxSize);
                    for(int j = 0 ; j < 3 ; j++) {
                        static int st_to_vec[6][3] = { // 1 = s, 2 = t, 3 = cloud?
                            { 3,  -1, 2 },
                            { -3, 1,  2 },
                            { 1,  3,  2 },
                            { -1, -3, 2 },
                            { -2, -1, 3 }, // 0 degrees yaw, look straight up
                            { 2,  -1, -3}   // look straight down
                        };
                        int k = st_to_vec[i][j];
                        if (k < 0) skyVec[j] = -b[-k - 1];
                        else skyVec[j] = b[k - 1];
                    }
                    // Computes parametric value 'p' that intersects with cloud layer
                    float radiusWorld = 4096;
                    float p = (1.0f / (2 * sq(skyVec))) *
                        (-2 * skyVec[2] * radiusWorld +
                         2 * sqrt(sq(skyVec[2]) * sq(radiusWorld) +
                                  2 * sq(skyVec[0]) * radiusWorld * sky->cloudHeight +
                                  sq(skyVec[0]) * sq(sky->cloudHeight) +
                                  2 * sq(skyVec[1]) * radiusWorld * sky->cloudHeight +
                                  sq(skyVec[1]) * sq(sky->cloudHeight) +
                                  2 * sq(skyVec[2]) * radiusWorld * sky->cloudHeight +
                                  sq(skyVec[2]) * sq(sky->cloudHeight)));
                    // Computes intersection point based on p
                    vec3 v = p * skyVec;
                    v.z += radiusWorld;
                    v = normalize(v);
                    float sRad = acos(v.x);
                    float tRad = acos(v.y);
                    vertices << Vertex(skyVec,vec2(sRad,tRad),0);
                }
            }

            for(int t = 0; t < (SKY_SUBDIVISIONS/2) - MIN_T; t++) {
                for(int s = 0; s < SKY_SUBDIVISIONS; s++) {
                    surface.addTriangle(vertices, firstIndex + s + 1 + t * (SKY_SUBDIVISIONS+1), firstIndex + s + (t + 1) * (SKY_SUBDIVISIONS+1), firstIndex + s + t * (SKY_SUBDIVISIONS+1));
                    surface.addTriangle(vertices, firstIndex + s + 1 + t * (SKY_SUBDIVISIONS+1), firstIndex + s + 1 + (t + 1) * (SKY_SUBDIVISIONS+1), firstIndex + s + (t + 1) * (SKY_SUBDIVISIONS+1));
                }
            }
        }
        surface.shader = sky;
        array<Surface> model; model << move(surface);
        models.insert(String("*skybox"_), move(model));
        entities.insert(String("sky"_)).insert(String("model"_), String("*skybox"_));

        /*if(shader.properties.contains("skyparms"_)) { // remove sky surfaces, parse sky params
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
    }

    /// Converts Entities to Objects
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
                Shader* shader = object.surface.shader;
                if(!shader || shader->name=="textures/common/caulk"_) shadowOnly << object;
                else {
                    shader->bind(); // Forces shader compilation for correct split
                    GLShader* id = shader->program;
                    /**/ if(shader->blendAdd) blendAdd[id] << object;
                    else if(shader->blendAlpha) blendAlpha[id] << object;
                    else if(shader->alphaTest) alphaTest[id] << object;
                    else opaque[id] << object;
                }
            }
        }
    }

    // WARNING: Object arrays shall not be reallocated after taking these pointers
    //for(array<Object>& a: opaque.values+alphaTest.values+blendAdd.values+blendAlpha.values/*except shadowOnly*/) for(Object& o: a) objects << &o;
    for(array<Object>& a: opaque.values) for(Object& o: a) objects << &o;
    for(array<Object>& a: alphaTest.values) for(Object& o: a) objects << &o;
    for(array<Object>& a: blendAdd.values) for(Object& o: a) objects << &o;
    for(array<Object>& a: blendAlpha.values) for(Object& o: a) objects << &o;

    /// Light Volume
    vec3 gridSize = toVec3(entities.at("worldspawn"_).at("gridsize"_));
    ref<float> worldBB = bsp.models()[0].boundingBox;
    gridMin = vec3(worldBB[0],worldBB[1],worldBB[2]);
    gridMax = vec3(worldBB[3],worldBB[4],worldBB[5]);
    int nx = floor(gridMax.x / gridSize.x) - ceil(gridMin.x / gridSize.x) + 1;
    int ny = floor(gridMax.y / gridSize.y) - ceil(gridMin.y / gridSize.y) + 1;
    int nz = floor(gridMax.z / gridSize.z) - ceil(gridMin.z / gridSize.z) + 1;
    assert(nx*ny*nz == bsp.lightVolume().size);
    buffer<byte4> lightData[3] = {buffer<byte4>(nx*ny*nz),buffer<byte4>(nx*ny*nz),buffer<byte4>(nx*ny*nz)};
    for(int i: range(nx*ny*nz)) { // Splits 9 components in 3 3D textures
        const ibspVoxel& voxel = bsp.lightVolume()[i];
        lightData[0][i] = byte4(voxel.ambient[2], voxel.ambient[1], voxel.ambient[0], 0);
        lightData[1][i] = byte4(voxel.directional[2], voxel.directional[1], voxel.directional[0], 0);
        float longitude = 2*PI*voxel.dir[0]/255.f, latitude = 2*PI*voxel.dir[1]/255.f;
        vec3 lightDirection = clip(vec3(0),round((vec3(cos(latitude)*sin(longitude),sin(longitude)*sin(latitude),cos(longitude))+vec3(1))/2.f*float(0xFF)), vec3(0xFF));
        lightData[2][i] = byte4(lightDirection[2], lightDirection[1], lightDirection[0], 0);
    }
    for(int z: range(nz)) for(int y: range(ny)) for(int x: range(nx)) {
        byte4& color = lightData[1][z*ny*nx+y*nx+x];
        // Fills direction for correct linear interpolation of directions
        int l = int(color[0])*int(color[0])+int(color[1])*int(color[1])+int(color[2])*int(color[2]);
        int alphaMax=0; //3*0x80*0x80;
        if(l>alphaMax) continue; // Only fills pixel under threshold
        byte4 direction = lightData[2][z*ny*nx+y*nx+x];
        for(int dz=-1;dz<1;dz++) for(int dy=-1;dy<1;dy++) for(int dx=-1;dx<1;dx++) {
            int offset = clip(0,z+dz,nz)*ny*nx+clip(0,y+dy,ny)*nx+clip(0,x+dx,nx);
            byte4& color = lightData[1][offset]; // Clamp coordinates
            int l = int(color[0])*int(color[0])+int(color[1])*int(color[1])+int(color[2])*int(color[2]);
            if(l > alphaMax) { alphaMax=l; direction=lightData[2][offset]; } // FIXME: alpha-weighted blend of neighbours
        }
        lightData[2][z*ny*nx+y*nx+x] = direction;
    }
    lightGrid[0] = GLTexture(nx,ny,nz,lightData[0]);
    lightGrid[1] = GLTexture(nx,ny,nz,lightData[1]);
    lightGrid[2] = GLTexture(nx,ny,nz,lightData[2]);
}

vec4 Scene::defaultPosition() const {
    const Entity& player = entities.value("info_player_intermission"_, entities.at("info_player_start"_));
    vec3 position = toVec3(player.at("origin"_));
    if(player.contains("angle"_)) return vec4(position, float(toDecimal(player.at("angle"_))*PI/180));
    else if(player.contains("target"_)) {
        vec3 forward = toVec3(targets.at(player.at("target"_)).at("origin"_))-position;
        return vec4(position, float(atan(forward.y,forward.x)-PI/2));
    } else error("Undefined default position"_);
}
