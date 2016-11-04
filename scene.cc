#include "scene.h"
#include "data.h"

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
template<> inline uint4 parse<uint4>(TextData& s) { return parseVec<uint4>(s); }
template<> inline vec3 parse<vec3>(TextData& s) { return parseVec<vec3>(s); }

Scene parseScene(ref<byte> file) {
    TextData s (file);
    while(s.match('#')) s.until('\n');
    if(s.match("ply\n")) {
        s.skip("format ascii 1.0\n");
        s.skip("comment "); s.until('\n');
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

        buffer<uint4> indices (faceCount);
        for(size_t i : range(faceCount)) {
            uint length = s.integer();
            if(length==3) {
                indices[i] = uint4(parse<uint3>(s), 0);
                indices[i][3] = indices[i][2]; // FIXME
            } else {
                assert_(length == 4, length);
                indices[i] = parse<uint4>(s);
            }
            s.until('\n');
        }

        const vec3 viewpoint (0,0,-64);
        buffer<Scene::Face> faces (faceCount, 0);
        for(uint4 face: indices) {
            faces.append({{vertices[face[0]], vertices[face[1]], vertices[face[2]], vertices[face[3]]},{0,1,1,0},{0,0,1,1},Image8()});
            //faces.append({{vertices[face[3]], vertices[face[2]], vertices[face[1]], vertices[face[0]]},{0,1,1,0},{0,0,1,1},Image8()});
        }
        return {viewpoint, ::move(faces)};
    } else {
        const vec3 viewpoint = parse<vec3>(s);
        s.skip('\n');
        array<Scene::Face> faces;
        while(s) {
            if(s.match('\n')) continue;
            if(s.match('#')) { s.until('\n'); continue; }
            array<vec3> polygon;
            while(s && !s.match('\n')) { // Empty line
                polygon.append(parse<vec3>(s));
                if(!s) break;
                s.skip('\n');
            }
            assert_(polygon.size == 4);
            faces.append({{polygon[3], polygon[2], polygon[1], polygon[0]},{0,1,1,0},{0,0,1,1},Image8()});
        }
        log(viewpoint, faces.size);
        return {viewpoint, ::move(faces)};
    }
}
