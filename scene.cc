#include "scene.h"
#include "data.h"

inline String str(const Scene::Face& face) { return str(face.position); }

// Enforces exact match for overload resolution
generic T parse(TextData&) { static_assert(0&&sizeof(T), "No overload for parse<T>(TextData&)"); }

template<> inline float parse<float>(TextData& s) { return s.decimal(); }
template<Type V> V parseVec(TextData& s) {
    V value;
    for(uint index: range(V::_N)) { s.whileAny(" \t"); value[index] = parse<Type V::_T>(s); }
    return value;
}
//template<> inline int2 parse<int2>(TextData& s) { return parseVec<int2>(s); }
template<> inline vec3 parse<vec3>(TextData& s) { return parseVec<vec3>(s); }


Scene parseScene(ref<byte> scene) {
    TextData s (scene);
    vec3 viewpoint;
    while(s.match('#')) s.until('\n');
    viewpoint = parse<vec3>(s);
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
        // Fan
        for(size_t i : range(1, polygon.size-1)) {
            //faces.append({{polygon[0], polygon[i], polygon[i+1]},{vec3(0,1,i==1), vec3(0,i==2,1)},1});
            faces.append({{polygon[i+1], polygon[i], polygon[0]},{vec3(i==1,1,0), vec3(1,i==2,0)},1});
        }
    }
    //log(viewpoint, faces);
    return {viewpoint, ::move(faces)};
}
