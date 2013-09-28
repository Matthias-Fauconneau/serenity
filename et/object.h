#pragma once
#include "shader.h"

struct Vertex {
    vec3 position; vec2 texcoord; vec3 normal; vec3 tangent; vec3 bitangent; float alpha,ambient; // 16f=64B
    vec2 lightmap; vec3 color;
    Vertex(vec3 position,vec2 texcoord,vec3 normal,float alpha=0,vec2 lightmap=0, vec3 color=1):position(position),texcoord(texcoord),normal(normal),alpha(alpha),lightmap(lightmap),color(color) {}
};

struct Surface {
    Surface(){}
    default_move(Surface);

    /// Adds a vertex and updates bounding box
    /// \return Index in vertex array
    uint addVertex(Vertex v);
    /// Adds a triangle to the surface by copying new vertices as needed
    /// \note Assumes identical sourceVertices between all calls
    /// \note Handles tangent basis generation
    void addTriangle(const ref<Vertex>& sourceVertices, int i1, int i2, int i3);
    ///
    void draw(GLShader& program, bool withTexcoord, bool withNormal, bool withAlpha, bool withTangent);
    ///
    bool raycast(vec3 origin, vec3 direction, float& z);

    vec3 bbMin,bbMax;
    map<int,int> indexMap;
    array<Vertex> vertices;
    array<uint> indices;
    GLVertexBuffer vertexBuffer;
    GLIndexBuffer indexBuffer;
    Shader* shader = 0;
};


struct Object {
    Object(Surface& surface) : surface(surface) {}

    Surface& surface;
    vec3 uniformColor {1,1,1};
    mat4 transform;
    vec3 center; vec3 extent; // Bounding box center and extent
    int planeIndex = 0; // Last hit bounding box plane index
};

struct Light {
    Light( vec3 origin, float radius, vec3 color, bool diffuse, bool specular, bool shadow) : origin(origin), radius(radius), color(color), diffuse(diffuse), specular(specular),shadow(shadow) {}
    //void project(const mat4& projection,const mat4& view);
    bool operator<(const Light& other) const { return -center.z < -other.center.z; }

    vec3 origin;
    float radius;
    vec3 color;
    bool diffuse,specular,shadow;
    vec3 clipMin,clipMax,center;
    int planeIndex = 0;
};
