#include "thread.h"
#include "data.h"
#include "vector.h"

struct Vertex {
    Vertex(vec3 position, vec3 texCoords, vec3 normal):position(position),texCoords(texCoords),normal(normal){}
    vec3 position;
    vec3 texCoords;
    vec3 normal;
};

struct Group {
    string name;
    string material;
    array<Vertex> vertices;
    array<uint> indices;
    array<array<int4>> indexMaps; // Remaps position/texCoords/normals indices to indices into group vertices
};

struct Sponza {
    Sponza() {
        Map map("Sponza/sponza.obj"_,home());
        string material = "sponza.mtl";
        array<vec3> positions; array<vec3> textureCoords; array<vec3> normals; //FIXME: reserve to avoid many copy on realloc
        Group group; array<int4>* indexMap = 0;
        for(TextData s(map);s.skip(), s;) {
            if(s.match('#')) s.line();
            else {
                string key = s.word();
                /**/  if(key=="mtllib"_) material = s.line();
                else if(key=="v"_) { vec3 p; for(int i: range(3)) { s.skip(); p[i] = s.decimal(); } positions << p; }
                else if(key=="vn"_) { vec3 p; for(int i: range(3)) { s.skip(); p[i] = s.decimal(); } normals << normalize(p); }
                else if(key=="vt"_) { vec3 p; for(int i: range(3)) { s.skip(); p[i] = s.decimal(); } textureCoords << p; }
                else if(key=="g"_) {
                    for(array<int4>& indexMap: group.indexMaps) indexMap.clear();
                    if(group.name) groups << move(group);
                    group = Group{s.line(),{},{},{},{}};
                }
                else if(key=="usemtl"_) { group.material = s.line(); }
                else if(key=="s"_) {
                    s.skip();
                    if(s.match("off"_)) indexMap=0;
                    else {
                        uint index = s.integer();
                        if(index>group.indexMaps.size) group.indexMaps.grow(index);
                        indexMap = &group.indexMaps[index-1];
                    }
                }
                else if(key=="f"_) {
                    for(int f=0; s.whileAny(" \t\r"_), !s.match('\n'); f++) {
                        int p=s.integer(); s.match('/'); int t=s.integer(); s.match('/'); int n=s.integer(); ;
                        int index =group.vertices.size;
                        if(indexMap) {
                            for(int4 i: *indexMap) if(i[0]==p && i[1]==t && i[2]==n) { index = i[3]; goto break_; }
                            /*else*/ {
                                *indexMap << int4(p,t,n,index);
                                group.vertices << Vertex(positions[p-1],textureCoords[t-1],normals[n-1]);
                            }
                        }
                        break_:;
                        if(f<3) group.indices << index;
                        else if(f==3) { // Repeats vertices to convert quads into two triangles
                            int size = group.indices.size;
                            group.indices << group.indices[size-3] << group.indices[size-1] << index;
                        }
                        else error(f);
                    }
                }
                else error(key, s.line());
            }
        }
    }

    array<Group> groups;
} application;
