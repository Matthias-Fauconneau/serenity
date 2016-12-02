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
    vec3 viewpoint = parse<vec3>(s);
    s.skip('\n');
    const size_t quadCount = parse<uint>(s);
    s.skip('\n');
    Scene scene (2*quadCount);
    // index=faceCount flags miss (raycast hits no face) (i.e background "face" color)
    scene.emittanceB[2*quadCount] = 0;
    scene.emittanceG[2*quadCount] = 0;
    scene.emittanceR[2*quadCount] = 0;
    scene.reflectanceB[2*quadCount] = 0;
    scene.reflectanceG[2*quadCount] = 0;
    scene.reflectanceR[2*quadCount] = 0;

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
        //const float gloss = 1./8;
        const float emittance = 8;
        const float reflectance = 1./8;
        { // Triangle ABC
            const vec3 T = normalize(polygon[1]-polygon[0]);
            const vec3 B = normalize(polygon[2]-polygon[1]);
            const vec3 cross = ::cross(T, B);
            const float lengthCross = length(cross);
            const vec3 N = cross/lengthCross;
            const float reflect = N.z == -1;
            bgr3f color = reflect==0 ? (bgr3f(N)+bgr3f(1))/2.f : bgr3f(1, 1./2, 1./2);

            scene.X0[faceIndex] = polygon[0].x-viewpoint.x;
            scene.Y0[faceIndex] = polygon[0].y-viewpoint.y;
            scene.Z0[faceIndex] = polygon[0].z-viewpoint.z;

            scene.X1[faceIndex] = polygon[1].x-viewpoint.x;
            scene.Y1[faceIndex] = polygon[1].y-viewpoint.y;
            scene.Z1[faceIndex] = polygon[1].z-viewpoint.z;

            scene.X2[faceIndex] = polygon[2].x-viewpoint.x;
            scene.Y2[faceIndex] = polygon[2].y-viewpoint.y;
            scene.Z2[faceIndex] = polygon[2].z-viewpoint.z;

            scene.U0[faceIndex] = 0;
            scene.U1[faceIndex] = 1;
            scene.U2[faceIndex] = 1;
            scene.U0[faceIndex] = 0;
            scene.U1[faceIndex] = 0;
            scene.U2[faceIndex] = 1;

            scene.TX0[faceIndex] = T.x;
            scene.TX1[faceIndex] = T.x;
            scene.TX2[faceIndex] = T.x;

            scene.TY0[faceIndex] = T.y;
            scene.TY1[faceIndex] = T.y;
            scene.TY2[faceIndex] = T.y;

            scene.TZ0[faceIndex] = T.z;
            scene.TZ1[faceIndex] = T.z;
            scene.TZ2[faceIndex] = T.z;

            scene.BX0[faceIndex] = B.x;
            scene.BX1[faceIndex] = B.x;
            scene.BX2[faceIndex] = B.x;

            scene.BY0[faceIndex] = B.y;
            scene.BY1[faceIndex] = B.y;
            scene.BY2[faceIndex] = B.y;

            scene.BZ0[faceIndex] = B.z;
            scene.BZ1[faceIndex] = B.z;
            scene.BZ2[faceIndex] = B.z;

            scene.NX0[faceIndex] = N.x;
            scene.NX1[faceIndex] = N.x;
            scene.NX2[faceIndex] = N.x;

            scene.NY0[faceIndex] = N.y;
            scene.NY1[faceIndex] = N.y;
            scene.NY2[faceIndex] = N.y;

            scene.NZ0[faceIndex] = N.z;
            scene.NZ1[faceIndex] = N.z;
            scene.NZ2[faceIndex] = N.z;

            if(N.y == 1 && polygon[0].y==0) {
                scene.emittanceB[faceIndex] = emittance;
                scene.emittanceG[faceIndex] = emittance;
                scene.emittanceR[faceIndex] = emittance;
                scene.reflectanceB[faceIndex] = reflectance;
                scene.reflectanceG[faceIndex] = reflectance;
                scene.reflectanceR[faceIndex] = reflectance;
                scene.lights.append( faceIndex );
                scene.area.append( lengthCross/2 );
                scene.CAF.append( (scene.CAF ? scene.CAF.last() : 0)+lengthCross/2 );
            } else {
                scene.emittanceB[faceIndex] = 0;
                scene.emittanceG[faceIndex] = 0;
                scene.emittanceR[faceIndex] = 0;
                color = reflectance * color / ::max(::max(color.b, color.g), color.r);
                scene.reflectanceB[faceIndex] = color.b;
                scene.reflectanceG[faceIndex] = color.g;
                scene.reflectanceR[faceIndex] = color.r;
            }

            // TODO: gloss
            // TODO: refract
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

            scene.X0[faceIndex] = polygon[0].x-viewpoint.x;
            scene.Y0[faceIndex] = polygon[0].y-viewpoint.y;
            scene.Z0[faceIndex] = polygon[0].z-viewpoint.z;

            scene.X1[faceIndex] = polygon[2].x-viewpoint.x;
            scene.Y1[faceIndex] = polygon[2].y-viewpoint.y;
            scene.Z1[faceIndex] = polygon[2].z-viewpoint.z;

            scene.X2[faceIndex] = polygon[3].x-viewpoint.x;
            scene.Y2[faceIndex] = polygon[3].y-viewpoint.y;
            scene.Z2[faceIndex] = polygon[3].z-viewpoint.z;

            scene.U0[faceIndex] = 0;
            scene.U1[faceIndex] = 1;
            scene.U2[faceIndex] = 0;
            scene.U0[faceIndex] = 0;
            scene.U1[faceIndex] = 1;
            scene.U2[faceIndex] = 1;

            scene.TX0[faceIndex] = T.x;
            scene.TX1[faceIndex] = T.x;
            scene.TX2[faceIndex] = T.x;

            scene.TY0[faceIndex] = T.y;
            scene.TY1[faceIndex] = T.y;
            scene.TY2[faceIndex] = T.y;

            scene.TZ0[faceIndex] = T.z;
            scene.TZ1[faceIndex] = T.z;
            scene.TZ2[faceIndex] = T.z;

            scene.BX0[faceIndex] = B.x;
            scene.BX1[faceIndex] = B.x;
            scene.BX2[faceIndex] = B.x;

            scene.BY0[faceIndex] = B.y;
            scene.BY1[faceIndex] = B.y;
            scene.BY2[faceIndex] = B.y;

            scene.BZ0[faceIndex] = B.z;
            scene.BZ1[faceIndex] = B.z;
            scene.BZ2[faceIndex] = B.z;

            scene.NX0[faceIndex] = N.x;
            scene.NX1[faceIndex] = N.x;
            scene.NX2[faceIndex] = N.x;

            scene.NY0[faceIndex] = N.y;
            scene.NY1[faceIndex] = N.y;
            scene.NY2[faceIndex] = N.y;

            scene.NZ0[faceIndex] = N.z;
            scene.NZ1[faceIndex] = N.z;
            scene.NZ2[faceIndex] = N.z;

            if(N.y == 1 && polygon[0].y==0) {
                scene.emittanceB[faceIndex] = emittance;
                scene.emittanceG[faceIndex] = emittance;
                scene.emittanceR[faceIndex] = emittance;
                scene.reflectanceB[faceIndex] = reflectance;
                scene.reflectanceG[faceIndex] = reflectance;
                scene.reflectanceR[faceIndex] = reflectance;
                scene.lights.append( faceIndex );
                scene.area.append( lengthCross/2 );
                scene.CAF.append( (scene.CAF ? scene.CAF.last() : 0)+lengthCross/2 );
            } else {
                scene.emittanceB[faceIndex] = 0;
                scene.emittanceG[faceIndex] = 0;
                scene.emittanceR[faceIndex] = 0;
                color = reflectance * color / ::max(::max(color.b, color.g), color.r);
                scene.reflectanceB[faceIndex] = color.b;
                scene.reflectanceG[faceIndex] = color.g;
                scene.reflectanceR[faceIndex] = color.r;
            }

            // TODO: gloss
            // TODO: refract
        }
        faceIndex++;
    }
    for(float& v: scene.area) v /= scene.CAF.last();
    for(float& v: scene.CAF) v /= scene.CAF.last();
    assert_(scene.CAF.last()==1);

    // Fits scene
    scene.min = inff, scene.max = -inff;
    scene.min = ::min(scene.min, vec3(::min(scene.X0), ::min(scene.Y0), ::min(scene.Z0)));
    scene.max = ::max(scene.max, vec3(::max(scene.X0), ::max(scene.Y0), ::max(scene.Z0)));
    scene.min = ::min(scene.min, vec3(::min(scene.X1), ::min(scene.Y1), ::min(scene.Z1)));
    scene.max = ::max(scene.max, vec3(::max(scene.X1), ::max(scene.Y1), ::max(scene.Z1)));
    scene.min = ::min(scene.min, vec3(::min(scene.X2), ::min(scene.Y2), ::min(scene.Z2)));
    scene.max = ::max(scene.max, vec3(::max(scene.X2), ::max(scene.Y2), ::max(scene.Z2)));
    scene.max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
    scene.scale = 2./::max(scene.max.x-scene.min.x, scene.max.y-scene.min.y);
    scene.near = scene.scale*scene.min.z;
    scene.far = scene.scale*scene.max.z;
    return scene;
}
