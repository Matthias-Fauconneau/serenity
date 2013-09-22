#pragma once
#include "matrix.h"

struct md3Frame {
    vec3 min;
    vec3 max;
    vec3 origin;
    float radius;
    char name[16];
};

struct md3Tag {
    char name[64];
    vec3 origin;
    mat3 rotation;
    static_assert(sizeof(mat3)==3*3*sizeof(float),"");
};


struct md3Shader {
    char name[64];
    uint index;
};

struct md3Vertex {
    short position[3]; // 6.6 fixed point
    uint8 latitude, longitude; // .8 fixed point spherical coordinates (angles)
};

typedef uint md3Triangle[3];

struct md3Surface {
    char magic[4]; // "IDP3"
    char name[64];
    uint flags;
#define LUMP(T,s,p) inline ref<T> p() const { return ref<T>((const T*)((const char*)this+s##Offset), s##Count ); }
    uint frameCount;
    uint shaderCount;
    uint vertexCount;
    uint triangleCount;
    uint triangleOffset; // Clock-wise winding
    uint shaderOffset;
    uint textureCoordinatesOffset; // upper-left origin
    uint vertexOffset;
    uint endOffset;
    LUMP(md3Shader, shader, shaders);
    LUMP(md3Vertex, vertex, vertices);
    LUMP(md3Triangle, triangle, triangles);
    inline ref<vec2> textureCoordinates() const { return ref<vec2>((const vec2*)((const char*)this+textureCoordinatesOffset), vertexCount); }
};

struct MD3 {
    char magic[4]; // "IDP3"
    int version; // 15
    char name[64];
    uint flags;
    uint frameCount;
    uint tagCount;
    uint surfaceCount;
    uint skinCount;
    uint frameOffset;
    uint tagOffset;
    uint surfaceOffset;
    uint endOffset;
    LUMP(md3Frame, frame, frames)
    LUMP(md3Tag, tag, tags)
    const md3Surface& surface(uint offset) const { return *(const md3Surface*)((const char*)this+offset); }
#undef LUMP
};
