#include "scene.h"
#include "data.h"

// Enforces exact match for overload resolution
generic T parse(TextData&) { static_assert(0&&sizeof(T), "No overload for parse<T>(TextData&)"); }

template<> inline float parse<float>(TextData& s) { return s.decimal(); }
template<Type V> V parseVec(TextData& s) {
    V value;
    for(uint index: range(V::_N)) { s.whileAny('\t'); value[index] = parse<Type V::_T>(s); }
    return value;
}
//template<> inline int2 parse<int2>(TextData& s) { return parseVec<int2>(s); }
template<> inline vec2 parse<vec2>(TextData& s) { return parseVec<vec2>(s); }


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
        while(!s.match('\n')) { // Empty line
            polygon.append(parse<vec3>(s));
            s.skip('\n');
        }
        assert_(polygon.size == 4);
    }
    return {viewpoint, ::move(faces)};
}
