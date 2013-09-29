#include "scene.h"
#include "object.h"

#include "material.h"
#include "bsp.h"
#include "md3.h"

#include "time.h"

vec3 toVec3(const string& data) { TextData s(data); vec3 v; for(uint i: range(3)) { v[i]=s.decimal(); if(i<2) s.skip(" "_); } return v; }

array<String> Scene::search(const string& query, const string& type) {
    array<String> files;
    string folder = section(query,'/',0,-2);
    string fileName = section(query,'/',-2,-1);
    if(existsFolder(folder,data)) for(const String& file: Folder(folder, data).list(Files)) if(startsWith(file, fileName) && endsWith(file, type)) files << folder+"/"_+file;
    return files;
}

Shader& Scene::getShader(string name, int lightmap) {
    Shader& shader = shaders[name];
    if(!shader.name) {
        shader.name=String(name);
        shader.append(Texture(name));
        shader.append(Texture("$lightmap"_));
    }
    for(Texture& texture: shader) {
        if(texture.path=="$lightmap"_) {
            if(lightmap>=0) {
                String lightMapID = "/lm_"_+dec(lightmap,4);
                Shader& lightMapShader = shaders[name+lightMapID];
                if(!lightMapShader.name) {
                    lightMapShader = copy(shader);
                    lightMapShader.name = name+lightMapID;
                    for(Texture& texture: lightMapShader) if(texture.path=="$lightmap"_) {
                        texture.type = String("lightmap"_);
                        texture.path = this->name+lightMapID; // Assumes extern "high resolution" lightmaps are always used (TODO: intern lightmaps)
                    }
                }
                return lightMapShader;
            } else if(lightmap==-2) {
                Shader& lightGridShader = shaders[name+"/lightgrid"_];
                if(!lightGridShader.name) {
                    lightGridShader = copy(shader);
                    lightGridShader.name = name+"/lightgrid"_;
                    lightGridShader.type << " lightgrid"_; // Evaluates lightgrid every vertex
                    for(Texture& texture: lightGridShader) if(texture.path=="$lightmap"_) texture.type = String("vertexlight"_);
                }
                return lightGridShader;
            } else texture.type = String("vertexlight"_);
        }
    }
    return shader;
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

array<Surface> Scene::importBSP(const BSP& bsp, int firstFace, int numFaces, bool leaf) {
    map<ID, Surface> surfaces;
    for(int f: range(firstFace,firstFace+numFaces)) {
        const bspFace& face = bsp.faces()[leaf?bsp.leafFaces()[f]:f];
        string name = str(bsp.shaders()[face.texture].name);
        Shader& shader = shaders[name];
        if(shader.properties.contains("skyparms"_)) { if(sky) assert_(sky==&shader); else sky=&shader; continue; }
        ID id{face.texture, max(-1,face.lightMapIndex)}; // Ensures surfaces are split by textures/lightmaps changes
        if(!surfaces.contains(id)) surfaces.insert(id, Surface(getShader(name, max(-1,face.lightMapIndex)), vertices));
        Surface& surface = surfaces.at(id);
        if(face.type==1||face.type==3) {
            for(int i=0;i<face.numIndices;i+=3) {
                surface.addTriangle(face.firstVertex+bsp.indices()[face.firstIndex+i+2],
                                                face.firstVertex+bsp.indices()[face.firstIndex+i+1],
                                                face.firstVertex+bsp.indices()[face.firstIndex+i+0]);
            }
        } else if(face.type==2) {
            int w = face.size[0], h = face.size[1];
            assert(w%2==1 && h%2==1 && w*h==face.numVertices && face.numIndices==0);
            const int level=4;
            vertices.reserve(vertices.size+(w/2)*(h/2)*sq(level+1));
            for(int y=0;y<h-2;y+=2) for(int x=0;x<w-2;x+=2) {
                ref<Vertex> control = vertices.slice(face.firstVertex+y*w+x,w*h);
                uint firstIndex = vertices.size;
                for(int j: range(level+1)) for(int k: range(level+1)) {
                    float u = float(j)/float(level), v = float(k)/float(level);
                    vertices << bezier(bezier(control[0*w+0],control[0*w+1],control[0*w+2],u),
                                        bezier(control[1*w+0],control[1*w+1],control[1*w+2], u),
                                        bezier(control[2*w+0],control[2*w+1],control[2*w+2], u), v);
                }
                for(int j: range(level)) for(int k: range(level)) {
                    uint i = firstIndex + j*(level+1) + k;
                    surface.addTriangle(i+(level+1), i+1, i+0);
                    surface.addTriangle(i+(level+1)+1, i+1, i+(level+1) );
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

        uint firstIndex = vertices.size;
        vertices.reserve(vertices.size+surface.vertexCount);
        for(uint i : range(surface.vertices().size)) {
            md3Vertex v = surface.vertices()[i];
            vec2 textureCoordinates = surface.textureCoordinates()[i];
            float latitude = v.latitude*(2*PI/255), longitude = v.longitude*(2*PI/255);
            vertices << Vertex(vec3(v.position[0]/64.f,v.position[1]/64.f,v.position[2]/64.f), vec2(textureCoordinates.x, textureCoordinates.y),
                                           vec3(cos(longitude)*sin(latitude),sin(longitude)*sin(latitude),cos(latitude)) );
        }

        Surface target(getShader(name), vertices);
        for(const md3Triangle& face: surface.triangles()) target.addTriangle(firstIndex+face[2], firstIndex+face[1], firstIndex+face[0] );

        surfaces << move(target);
    }
    return surfaces;
}

Scene::Scene(string file, const Folder& data) : data(data) {
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

    /// Parses shader scripts
    array<String> materials = search("scripts/"_,".shader"_);
    if(!materials) error("No scripts/*.shader material shader script files found");
    for(string path: materials) shaders << parseMaterialFile(readFile(path,data));

    /// Loads BSP geometry
    vertices.reserve(bsp.vertices().size); // Reserves more to append bezier patches and md3 models
    for(uint i: range(bsp.vertices().size)) {
        const ibspVertex& v = bsp.vertices()[i];
        vertices << Vertex(v.position, vec2(v.texture.x,v.texture.y), v.normal, v.color[3]/255.f, vec2(v.lightmap.x,v.lightmap.y),
                vec3(v.color[0]/255.f,v.color[1]/255.f,v.color[2]/255.f));
    }
    for(uint i: range(bsp.models().size)) {
        const bspModel& model = bsp.models()[i];
        models["*"_+str(i)]<< importBSP(bsp, model.firstFace, model.numFaces, false);
    }

    if(sky) {
        float boxSize = 32768 / sqrt(3.); // Corners at zFar
        { // Sky
            sky->skyBox = true;
            sky->final.clear(); // Disable fog

            const int skySubdivisions = 8;
            vertices.reserve(vertices.size+5*skySubdivisions*skySubdivisions);
            Surface surface(*sky, vertices);
            for(int i = 0; i < 5; i++) {
                float MIN_T = i==4 ? -(skySubdivisions/2) : 0;
                uint firstIndex = vertices.size;

                for(int t = MIN_T + (skySubdivisions/2); t <= skySubdivisions; t++) { // Iterates through the subdivisions
                    for(int s = 0; s <= skySubdivisions; s++) { // Compute vector from view origin to sky side integral point
                        vec3 skyVec;
                        vec3 b (
                                    ((s - (skySubdivisions/2)) / ( float ) (skySubdivisions/2)) * boxSize,
                                    ((t - (skySubdivisions/2)) / ( float ) (skySubdivisions/2)) * boxSize,
                                    boxSize);
                        for(int j = 0 ; j < 3 ; j++) {
                            static int st_to_vec[6][3] = {{ 3,  -1, 2 }, { -3, 1,  2 }, { 1,  3,  2 }, { -1, -3, 2 }, { -2, -1, 3 }};
                            int k = st_to_vec[i][j];
                            skyVec[j] = k < 0 ? -b[-k - 1] : b[k - 1];
                        }
                        // Computes parametric value 'p' that intersects with cloud layer
                        float radiusWorld = 4096;
                        float cloudHeight = toDecimal(sky->properties.at("skyparms"_));
                        float p = (sqrt(sq(skyVec.z) * sq(radiusWorld) +
                                        2 * sq(skyVec[0]) * radiusWorld * cloudHeight + sq(skyVec[0]) * sq(cloudHeight) +
                                        2 * sq(skyVec[1]) * radiusWorld * cloudHeight + sq(skyVec[1]) * sq(cloudHeight) +
                                        2 * sq(skyVec[2]) * radiusWorld * cloudHeight + sq(skyVec[2]) * sq(cloudHeight)) - skyVec.z * radiusWorld) / sq(skyVec);
                        vec3 v = p * skyVec; v.z += radiusWorld; v = normalize(v);
                        float sRad = acos(v.x);
                        float tRad = acos(v.y);
                        vertices << Vertex(skyVec,vec2(sRad,tRad),0);
                    }
                }

                for(int t = 0; t < (skySubdivisions/2) - MIN_T; t++) {
                    for(int s = 0; s < skySubdivisions; s++) {
                        surface.addTriangle(
                                    firstIndex + s + 1 + t * (skySubdivisions+1),
                                    firstIndex + s + (t + 1) * (skySubdivisions+1),
                                    firstIndex + s + t * (skySubdivisions+1));
                        surface.addTriangle(
                                    firstIndex + s + 1 + t * (skySubdivisions+1),
                                    firstIndex + s + 1 + (t + 1) * (skySubdivisions+1),
                                    firstIndex + s + (t + 1) * (skySubdivisions+1));
                    }
                }
            }
            array<Surface> model; model << move(surface);
            models.insert(String("*skybox"_), move(model));
            entities.insert(String("sky"_)).insert(String("model"_), String("*skybox"_));
        }
        { // Sun
            Shader& shader = getShader(sky->properties.at("sunshader"_));
            assert_(shader.blendAlpha);
            shader.skyBox = true;
            shader.final.clear(); // Disable fog

            array<string> sun = split(sky->properties.at("q3map_sun"_),' ');
            float azimuth = toDecimal(sun[4])*PI/180, elevation=toDecimal(sun[5])*PI/180;
            vec3 sunDirection = vec3(cos(azimuth)*cos(elevation),sin(azimuth)*cos(elevation),sin(elevation));
            float sunSize = boxSize * 0.2; // ~ 6 degrees
            vec3 center = boxSize * sunDirection;
            vec3 u = normal(sunDirection);
            vec3 v = cross(sunDirection, u);

            uint firstIndex = vertices.size;
            vertices << Vertex(center+sunSize*(-u-v),vec2(0,0),0) << Vertex(center+sunSize*(-u+v),vec2(1,0),0)
                     << Vertex(center+sunSize*(+u+v),vec2(1,1),0) << Vertex(center+sunSize*(+u-v),vec2(0,1),0);
            Surface surface(shader, vertices);
            surface.addTriangle(firstIndex+0, firstIndex+1, firstIndex+2);
            surface.addTriangle(firstIndex+2, firstIndex+3, firstIndex+0);

            array<Surface> model; model << move(surface);
            models.insert(String("*sun"_), move(model));
            entities.insert(String("sun"_)).insert(String("model"_), String("*sun"_));
        }
    }
    {// Fog
        Shader& fog = shaders.at(entities.at("worldspawn"_).at("_fog"_));
        array<double> v = apply(split(fog.properties.at("fogparms"_)), toDecimal);
        this->fog = vec4(v[0],v[1],v[2],v[3]);
    }

    /// Converts Entities to Objects (and compile shaders)
    for(const Entity& e: entities.values) if(e.contains("model"_)) {
        string name = e.at("model"_);
        if(!models.contains(name) && !startsWith(name,"*"_)) models[name]=importMD3(search(section(name,'.',0,-2)+"."_,"md3"_).first());
        if(!models.contains(name)) { error(name); continue; }
        if(!models.at(name)) continue; // Some BSP models are empty
        mat4 transform;
        if(e.contains("target"_) && targets.at(e.at("target"_)).contains("origin"_)) transform.translate(toVec3(targets.at(e.at("target"_)).at("origin"_)));
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
            Shader& shader = object.surface.shader;
            shader.bind(); // Forces shader compilation for correct split
            GLShader* id = shader.program;
            if(shader.blendAlpha) blendAlpha[id] << object;
            else if(shader.blendColor) error("blendColor");
            else opaque[id] << object;
        }
    }

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

    // WARNING: Object arrays shall not be reallocated after taking these pointers
    for(array<Object>& a: opaque.values) for(Object& o: a) objects << &o;
    for(array<Object>& a: blendAlpha.values) for(Object& o: a) objects << &o;
    for(array<Object>& a: blendColor.values) for(Object& o: a) objects << &o;

    for(Object* object: objects) for(Texture& texture: object->surface.shader) if(texture.path!="$lightmap"_ && !texture.texture) {
        String& path = texture.path;
        if(endsWith(path,".tga"_)) path=String(section(path,'.',0,-2));
        if(!textures.contains(path)) {
            Map file;
            if(existsFile(path,data)) file=Map(path,data);
            else if(existsFile(path+".png"_,data)) file=Map(path+".png"_,data);
            else if(existsFile(path+".jpg"_,data)) file=Map(path+".jpg"_,data);
            else if(existsFile(path+".tga"_,data)) file=Map(path+".tga"_,data);
            assert_(file, data.name()+"/"_+path);
            textures.insert(copy(path), upload(file));
        }
        texture.texture = textures.at(path).pointer;
    }

    uint indexCount=0; for(Object* object: objects) indexCount+=object->surface.indices.size;
    log(opaque.size()+blendAlpha.size()+blendColor.size(),"shaders", objects.size,"objects",vertices.size,"vertices",indexCount/3,"faces");
    //for(Object* object: objects) log(object->surface.shader.name);
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
