#include "scene.h"
#include "data.h"
#include "variant.h"
#include <sys/mman.h>

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
    Scene scene;
    vec3 viewpoint = parse<vec3>(s);
    s.skip('\n');
    const size_t quadCount = parse<uint>(s);
    s.skip('\n');
    scene.faces = buffer<Scene::Face>(align(8,2*quadCount), 2*quadCount);
    scene.emittanceB = buffer<float>(align(8,2*quadCount+1), 2*quadCount+1);
    scene.emittanceG = buffer<float>(align(8,2*quadCount+1), 2*quadCount+1);
    scene.emittanceR = buffer<float>(align(8,2*quadCount+1), 2*quadCount+1);
    scene.reflectanceB = buffer<float>(align(8,2*quadCount+1), 2*quadCount+1);
    scene.reflectanceG = buffer<float>(align(8,2*quadCount+1), 2*quadCount+1);
    scene.reflectanceR = buffer<float>(align(8,2*quadCount+1), 2*quadCount+1);
    // index=faceCount flags miss (raycast hits no face) (i.e background "face" color)
    scene.emittanceB[2*quadCount] = 0;
    scene.emittanceG[2*quadCount] = 0;
    scene.emittanceR[2*quadCount] = 0;
    scene.reflectanceB[2*quadCount] = 0;
    scene.reflectanceG[2*quadCount] = 0;
    scene.reflectanceR[2*quadCount] = 0;
    for(size_t i: range(3)) {
        scene.X[i] = buffer<float>(align(8,2*quadCount), 2*quadCount);
        scene.Y[i] = buffer<float>(align(8,2*quadCount), 2*quadCount);
        scene.Z[i] = buffer<float>(align(8,2*quadCount), 2*quadCount);
    }

    size_t faceIndex = 0;
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
        const float gloss = 1./8;
        { // Triangle ABC
            const vec3 T = normalize(polygon[1]-polygon[0]);
            const vec3 B = normalize(polygon[2]-polygon[1]);
            const vec3 cross = ::cross(T, B);
            const float lengthCross = length(cross);
            const vec3 N = cross/lengthCross;
            const float reflect = N.z == -1;
            bgr3f color = reflect==0 ? (bgr3f(N)+bgr3f(1))/2.f : bgr3f(1, 1./2, 1./2);
            scene.faces[faceIndex] = {{0,1,1},{0,0,1},{T,T,T},{B,B,B},{N,N,N},reflect,0,gloss,0,0};
            for(size_t i: range(3)) {
                scene.X[i][faceIndex] = polygon[i].x-viewpoint.x;
                scene.Y[i][faceIndex] = polygon[i].y-viewpoint.y;
                scene.Z[i][faceIndex] = polygon[i].z-viewpoint.z;
            }
            if(N.y == 1 && polygon[0].y==0) {
                scene.emittanceB[faceIndex] = 1;
                scene.emittanceG[faceIndex] = 1;
                scene.emittanceR[faceIndex] = 1;
                scene.reflectanceB[faceIndex] = 1;
                scene.reflectanceG[faceIndex] = 1;
                scene.reflectanceR[faceIndex] = 1;
                scene.lights.append( faceIndex );
                scene.area.append( lengthCross/2 );
                scene.CAF.append( (scene.CAF ? scene.CAF.last() : 0)+lengthCross/2 );
            } else {
                scene.emittanceB[faceIndex] = 0;
                scene.emittanceG[faceIndex] = 0;
                scene.emittanceR[faceIndex] = 0;
                color = color / ::max(::max(color.b, color.g), color.r);
                scene.reflectanceB[faceIndex] = color.b;
                scene.reflectanceG[faceIndex] = color.g;
                scene.reflectanceR[faceIndex] = color.r;
            }
        }
        faceIndex++;
        { // Triangle ACD
            const vec3 T = normalize(polygon[2]-polygon[3]);
            const vec3 B = normalize(polygon[3]-polygon[0]);
            const vec3 cross = ::cross(T, B);
            const float lengthCross = length(cross);
            const vec3 N = cross/lengthCross;
            const float reflect = N.z == -1;
            bgr3f color = reflect==0 ? (bgr3f(N)+bgr3f(1))/2.f : bgr3f(1, 1./2, 1./2);
            scene.faces[faceIndex] = {{0,1,0},{0,1,1},{T,T,T},{B,B,B},{N,N,N},reflect,0,gloss,0,0};
            for(size_t i: range(3)) {
                scene.X[i][faceIndex] = polygon[i?1+i:0].x-viewpoint.x;
                scene.Y[i][faceIndex] = polygon[i?1+i:0].y-viewpoint.y;
                scene.Z[i][faceIndex] = polygon[i?1+i:0].z-viewpoint.z;
            }
            if(N.y == 1 && polygon[0].y==0) {
                scene.emittanceB[faceIndex] = 1;
                scene.emittanceG[faceIndex] = 1;
                scene.emittanceR[faceIndex] = 1;
                scene.reflectanceB[faceIndex] = 1;
                scene.reflectanceG[faceIndex] = 1;
                scene.reflectanceR[faceIndex] = 1;
                scene.lights.append( faceIndex );
                scene.area.append( lengthCross/2 );
                scene.CAF.append( (scene.CAF ? scene.CAF.last() : 0)+lengthCross/2 );
            } else {
                scene.emittanceB[faceIndex] = 0;
                scene.emittanceG[faceIndex] = 0;
                scene.emittanceR[faceIndex] = 0;
                color = color / ::max(::max(color.b, color.g), color.r);
                scene.reflectanceB[faceIndex] = color.b;
                scene.reflectanceG[faceIndex] = color.g;
                scene.reflectanceR[faceIndex] = color.r;
            }
        }
        faceIndex++;
    }
    assert_(scene.faces.size == 2*quadCount);
    for(float& v: scene.area) v /= scene.CAF.last();
    for(float& v: scene.CAF) v /= scene.CAF.last();
    assert_(scene.CAF.last()==1);
    scene.fit();
    return scene;
}
