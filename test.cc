#include "thread.h"
#include "data.h"
#include "vector.h"

//include "parse.h"
// Enforces exact match for overload resolution
generic T parse(TextData&) { static_assert(0&&sizeof(T), "No overload for parse<T>(TextData&)"); }
template<> inline uint parse<uint>(TextData& s) { return s.integer(false); }
template<> inline float parse<float>(TextData& s) { return s.decimal(); }
template<Type V> V parseVec(TextData& s) {
    V value;
    for(uint index: range(V::_N)) { s.whileAny(" \t"); value[index] = parse<Type V::_T>(s); }
    return value;
}
template<> inline uint3 parse<uint3>(TextData& s) { return parseVec<uint3>(s); }
template<> inline vec3 parse<vec3>(TextData& s) { return parseVec<vec3>(s); }

string basename(string x) {
    string name = x.contains('/') ? section(x,'/',-2,-1) : x;
    string basename = name.contains('.') ? section(name,'.',0,-2) : name;
    assert_(basename);
    return basename;
}

struct Test {
    Test() {
        TextData s = readFile(basename(arguments()[0])+".ply");
        s.skip("ply\n");
        s.skip("format ascii 1.0\n");
        s.skip("element vertex ");
        const uint vertexCount = s.integer();
        s.skip('\n');
        while(s.match("property")) s.until("\n");
        s.skip("element face ");
        const uint faceCount = s.integer();
        s.skip('\n');
        while(s.match("property")) s.until("\n");
        s.skip("end_header\n");
        buffer<vec3> vertices (vertexCount);
        for(size_t i : range(vertexCount)) {
            vertices[i] = parse<vec3>(s);
            s.until('\n');
        }

        vec3 min = inff, max = -inff;
        for(vec3 p: vertices) { min = ::min(min, p); max = ::max(max, p); }
        log(min, max);

        buffer<uint3> faces (faceCount);
        for(size_t i : range(faceCount)) {
            faces[i] = parse<uint3>(s);
            s.until('\n');
        }
        array<char> scene;
        scene.append("0 0 0\n\n");
        for(uint3 face: faces) {
            scene.append(str(vertices[face[0]])+'\n'+str(vertices[face[1]])+'\n'+str(vertices[face[2]])+"\n\n");
        }
        writeFile(basename(arguments()[0])+".scene", scene);
    }
} app;
