#pragma once
#include "matrix.h"
#include "simd.h"

#define HALF 0 // FIXME: Accumulate singles, sample halfs
#if HALF
#error
typedef half Float;
#else
typedef float Float;
#endif

struct Scene {
    const size_t size;
    Scene(size_t size) : size(size) {}
    const size_t capacity = align(8, size+1);
    // Vertex attributes
    buffer<float> X0 {size, capacity};
    buffer<float> X1 {size, capacity};
    buffer<float> X2 {size, capacity};
    buffer<float> Y0 {size, capacity};
    buffer<float> Y1 {size, capacity};
    buffer<float> Y2 {size, capacity};
    buffer<float> Z0 {size, capacity};
    buffer<float> Z1 {size, capacity};
    buffer<float> Z2 {size, capacity};
    buffer<float> U0 {size, capacity};
    buffer<float> U1 {size, capacity};
    buffer<float> U2 {size, capacity};
    buffer<float> V0 {size, capacity};
    buffer<float> V1 {size, capacity};
    buffer<float> V2 {size, capacity};
    buffer<float> TX0 {size, capacity};
    buffer<float> TX1 {size, capacity};
    buffer<float> TX2 {size, capacity};
    buffer<float> TY0 {size, capacity};
    buffer<float> TY1 {size, capacity};
    buffer<float> TY2 {size, capacity};
    buffer<float> TZ0 {size, capacity};
    buffer<float> TZ1 {size, capacity};
    buffer<float> TZ2 {size, capacity};
    buffer<float> BX0 {size, capacity};
    buffer<float> BX1 {size, capacity};
    buffer<float> BX2 {size, capacity};
    buffer<float> BY0 {size, capacity};
    buffer<float> BY1 {size, capacity};
    buffer<float> BY2 {size, capacity};
    buffer<float> BZ0 {size, capacity};
    buffer<float> BZ1 {size, capacity};
    buffer<float> BZ2 {size, capacity};
    buffer<float> NX0 {size, capacity};
    buffer<float> NX1 {size, capacity};
    buffer<float> NX2 {size, capacity};
    buffer<float> NY0 {size, capacity};
    buffer<float> NY1 {size, capacity};
    buffer<float> NY2 {size, capacity};
    buffer<float> NZ0 {size, capacity};
    buffer<float> NZ1 {size, capacity};
    buffer<float> NZ2 {size, capacity};
     // Face attributes
    buffer<float> emittanceB {size, capacity};
    buffer<float> emittanceG {size, capacity};
    buffer<float> emittanceR {size, capacity};
    buffer<float> reflectanceB {size, capacity};
    buffer<float> reflectanceG {size, capacity};
    buffer<float> reflectanceR {size, capacity};
    // buffer<float> gloss {size, capacity}; 0=specular .. 1=diffuse
    // buffer<float> refract {size, capacity};

    buffer<uint> BGR {size, capacity};
    buffer<uint> BGRst {size, capacity}; // s,t dependent
    buffer<uint> size1 {size, capacity};
    buffer<uint> size2 {size, capacity};
    buffer<uint> size4 {size, capacity};
    buffer<v8si> sample4D {size, capacity};
    buffer<v4sf> Wts {size, capacity};

    array<uint> lights; // Face index of lights
    array<float> area; // Area of lights (sample proportionnal to area) (Divided by sum)
    array<float> CAF; // Cumulative area of lights (sample proportionnal to area)

    vec3 min, max;
    float scale, near, far;

    Float* base = 0;
    uint iterations = 0;
};


void setSTSize(const uint sSize, const uint tSize) {
    for(size_t faceIndex: range(scene.size)) {
        const int    size1 = scene.size1[faceIndex];
        const int    size2 = scene.size2[faceIndex];
        const int    size3 = sSize      *size2;
        const size_t size4 = tSize      *size3;
        scene.size4[faceIndex] = size4;
#if HALF // Half
        scene.sample4D[faceIndex] = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                   size3/2,   (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
        if(sSize == 1 || tSize ==1) // Prevents OOB
            sample4D[faceIndex] = {    0,           size1/2,         0,       size1/2,
                                       0,           size1/2,         0,       size1/2};
#else  // Single
        scene.sample4D[faceIndex] = {    0,           size1,         size2,       (size2+size1),
                                     size3,   (size3+size1), (size3+size2), (size3+size2+size1)};
        if(sSize == 1 || tSize ==1) // Prevents OOB
            scene.sample4D[faceIndex] = {    0,           size1,         0,       size1,
                                             0,           size1,         0,       size1};
#endif
    }
}

void setFaceAttributes(const uint sSize, const uint tSize, const float S, const float T) {
    const float s = ::min(S * (sSize-1), sSize-1-0x1p-18f);
    const float t = ::min(T * (tSize-1), tSize-1-0x1p-18f);
    const size_t sIndex = s;
    const size_t tIndex = t;
    for(size_t faceIndex: range(scene.size)) {
        const int    size2 = scene.size2[faceIndex];
        const int    size3 = sSize      *size2;
         // FIXME: TODO: store diffuse (average s,t) texture for non-primary (diffuse) evaluation
        scene.BGRst[faceIndex] = scene.BGR[faceIndex] + tIndex*size3 + sIndex*size2;
        scene.Wts[faceIndex] = {(1-fract(t))*(1-fract(s)), (1-fract(t))*fract(s), fract(t)*(1-fract(s)), fract(t)*fract(s)};
    }
}

inline bgr3f sample(const Scene& scene, const uint face, const float u, const float v) {
    const int vIndex = v, uIndex = u; // Floor
#if HALF // Half
    const size_t base = 2*sample4D[face][1]*vIndex + uIndex;
    const v16sf B = toFloat((v16hf)gather((float*)(face.BGR[0]+base), sample4D[face]));
    const v16sf G = toFloat((v16hf)gather((float*)(face.BGR[1]+base), sample4D[face]));
    const v16sf R = toFloat((v16hf)gather((float*)(face.BGR[2]+base), sample4D[face]));
#else  // Single
    const size_t base = scene.sample4D[face][1]*vIndex + uIndex;
    const v8sf b0 = gather((float*)(scene.BGR[0]+base), scene.sample4D[face]);
    const v8sf b1 = gather((float*)(scene.BGR[0]+base+1), scene.sample4D[face]);
    const v16sf B = shuffle(b0, b1, 0, 8+0, 1, 8+1, 2, 8+2, 3, 8+3, 4, 8+4, 5, 8+5, 6, 8+6, 7, 8+7);
    const v8sf g0 = gather((float*)(scene.BGR[1]+base), scene.sample4D[face]);
    const v8sf g1 = gather((float*)(scene.BGR[1]+base+1), scene.sample4D[face]);
    const v16sf G = shuffle(g0, g1, 0, 8+0, 1, 8+1, 2, 8+2, 3, 8+3, 4, 8+4, 5, 8+5, 6, 8+6, 7, 8+7);
    const v8sf r0 = gather((float*)(scene.BGR[2]+base), scene.sample4D[face]);
    const v8sf r1 = gather((float*)(scene.BGR[2]+base+1), scene.sample4D[face]);
    const v16sf R = shuffle(r0, r1, 0, 8+0, 1, 8+1, 2, 8+2, 3, 8+3, 4, 8+4, 5, 8+5, 6, 8+6, 7, 8+7);
#endif
    const v4sf Wts = scene.Wts[face]; // FIXME: TODO: store diffuse (average s,t) texture for non-primary (diffuse) evaluation
    const v4sf vuvu = {v, u, v, u};
    const v4sf w_1mw = abs(vuvu - floor(vuvu) - _1100f); // 1-fract(x), fract(x)
    const v16sf w01 = shuffle(  Wts,   Wts, 0,0,0,0,1,1,1,1, 2,2,2,2,3,3,3,3)  // 0000111122223333
            * shuffle(w_1mw, w_1mw, 0,0,2,2,0,0,2,2, 0,0,2,2,0,0,2,2)  // vvVVvvVVvvVVvvVV
            * shuffle(w_1mw, w_1mw, 1,3,1,3,1,3,1,3, 1,3,1,3,1,3,1,3); // uUuUuUuUuUuUuUuU
    return bgr3f(dot(w01, B), dot(w01, G), dot(w01, R));
}


Scene parseScene(ref<byte> scene);

inline string basename(string x) {
    string name = x.contains('/') ? section(x,'/',-2,-1) : x;
    string basename = name.contains('.') ? section(name,'.',0,-2) : name;
    assert_(basename);
    return basename;
}

#include "file.h"

inline String sceneFile(string name) {
    if(existsFile(name)) return name+"/scene.json";
    if(existsFile(name+".scene")) return name+".scene";
    if(existsFile(name+".obj")) return name+".obj";
    error("No such file", name);
}
