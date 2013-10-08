#include "scene.h"
#include "thread.h"
#include "file.h"
#include "data.h"
#include "image.h"

vec3 parseVec3(TextData& s) { vec3 v; for(uint i: range(3)) { v[i]=s.decimal(); if(i<2) s.skip(" "_); } return v; }

map<String, shared<Material> > parseMaterials(string file) {
    map<String, shared<Material>> materials;
    Material* material = 0;
    for(TextData s(file);s.skip(), s;) {
        if(s.match('#')) s.line();
        else {
            string key = s.word("_"); s.whileAny(" \t"_);
            if(key=="newmtl"_) {
                string name = s.identifier("_"_);
                material = materials.insert(String(name), shared<Material>(name)).pointer;
            }
            else if(key=="Ns"_) /*material->specularExponent =*/ s.decimal();
            else if(key=="Ni"_) s.decimal(); // Refraction index ?
            else if(key=="d"_ || key=="Tr"_) /*material->transparency =*/ s.decimal();
            else if(key=="Tf"_) s.line(); // Transmitted color
            else if(key=="illum"_) assert_(s.integer()==2);
            else if(key=="Ka"_ || key=="Kd"_) parseVec3(s);
            else if(key=="Ks"_) parseVec3(s);
            else if(key=="Ke"_) assert_(parseVec3(s)==vec3(0));
            else if(key=="map_Ka"_||key=="map_Kd"_) {
                string path = section(section(s.identifier("/\\_."), '\\', -2, -1), '.');
                assert(!material->diffusePath || path==material->diffusePath);
                material->diffusePath = String(path);
            }
            else if(key=="map_d"_) material->maskPath = String(section(section(s.identifier("/\\_."), '\\', -2, -1), '.'));
            else if(key=="bump"_||key=="map_bump"_) material->normalPath = String(section(section(s.identifier("/\\_."), '\\', -2, -1), '.'));
            else error(key, s.line());
        }
    }
    return materials;
}

Scene::Scene() {
    Folder folder("Sponza"_,home());
    Map file("sponza.obj"_,folder);
    map<String, shared<Material>> materials;
    array<vec3> positions (1<<17); array<vec2> textureCoords (1<<17); array<vec3> normals (1<<17);
    array<Surface> surfaces; string group; Surface* surface = 0;
    array<array<ptni>> indexMaps; array<ptni>* indexMap = 0; // Remaps position/texCoords/normals indices to indices into surface vertices
    for(TextData s(file);s.skip(), s;) {
        if(s.match('#')) s.line();
        else {
            string key = s.word(); s.whileAny(" \t"_);
            /**/  if(key=="mtllib"_) { materials = parseMaterials(readFile(s.identifier("."_),folder)); }
            else if(key=="v"_) positions << parseVec3(s);
            else if(key=="vn"_) normals << normalize(parseVec3(s));
            else if(key=="vt"_) { vec3 t = parseVec3(s); textureCoords << vec2(t.x,1-t.y); } //E z!=0
            else if(key=="g"_) group = s.identifier("_"_);
            else if(key=="usemtl"_) {
                indexMaps.clear();
                surfaces << Surface(group);
                surface = &surfaces.last();
                surface->material = share(materials.at(s.identifier("_")));
            }
            else if(key=="s"_) {
                s.skip();
                if(s.match("off"_)) indexMap=0;
                else {
                    uint index = s.integer();
                    if(index>indexMaps.size) indexMaps.grow(index);
                    indexMap = &indexMaps[index-1];
                }
            }
            else if(key=="f"_) {
                assert(surface);
                for(int f=0; s.whileAny(" \t\r"_), !s.match('\n'); f++) {
                    int p=s.integer(); s.match('/'); int t=s.integer(); s.match('/'); int n=s.integer(); ;
                    int index;
                    if(indexMap) for(ptni e: *indexMap) if(e.p==p && e.t==t && e.n==n) { index = e.i; goto break_; }
                    /*else*/ {
                        index = surface->vertices.size;
                        if(indexMap) *indexMap << ptni{p,t,n,index};
                        surface->vertices << Vertex(positions[p-1],textureCoords[t-1],normals[n-1]);
                    }
                    break_:;
                    if(f<3) surface->indices << index;
                    else if(f==3) { // Repeats vertices to convert quads into two triangles
                        int size = surface->indices.size; assert_(size>=3 && size%3==0);
                        int a = surface->indices[size-3], b = surface->indices[size-1], c = index;
                        surface->indices << a;
                        surface->indices << b;
                        surface->indices << c;
                    }
                    else error(f);
                }
            }
            else error(key, s.line());
        }
    }
    for(Surface& surface : surfaces) {
        Material& material = surface.material;
        if(!material.diffuseTexture /*&& material.colorPath*/) {
            String path = material.diffusePath+".png"_;
            if(!existsFile(path,folder)) continue;
            Image image = decodeImage(Map(path, folder));
            if(material.maskPath) {
                Image mask = decodeImage(Map(material.maskPath+".png"_, folder));
                assert(image.size()==mask.size() && image.stride == mask.stride);
                for(uint i : range(image.width*image.height)) image.buffer[i].a = mask.buffer[i].g;
                image.alpha =true;
            }
            material.diffuseTexture = GLTexture(image, Bilinear|Mipmap|Anisotropic);
        }
        for(const Vertex& vertex: surface.vertices) {
            worldMin = min(worldMin, vertex.position);
            worldMax = max(worldMax, vertex.position);
        }
        surface.vertexBuffer.upload<Vertex>(surface.vertices);
        assert_(surface.indices.size>=3 && surface.indices.size%3==0);
        surface.indexBuffer.upload(surface.indices);
        ((material.diffuseTexture.format&7)==sRGBA ? blend : replace) << move(surface);
    }
}
