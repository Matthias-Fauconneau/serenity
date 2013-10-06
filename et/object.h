#pragma once
#include "shader.h"

struct Vertex {
    vec3 position; float alpha; vec2 texcoord; vec2 lightmap; vec3 normal; vec3 color; // 14
    Vertex(vec3 position,vec2 texcoord,vec3 normal,float alpha=0,vec2 lightmap=0, vec3 color=1) :
        position(position), alpha(alpha), texcoord(texcoord), lightmap(lightmap), normal(normal), color(color) {}
};

struct VertexBuffer : array<Vertex>, GLVertexBuffer {};

struct Surface {
    Surface(Shader& shader, VertexBuffer& vertices, array<uint>&& indices={});
    default_move(Surface);

    /// Appends a triangle to the surface
    void addTriangle(int a, int b, int c);
    /// Renders the surface using \a program
    void draw(GLShader& program);
    /// Intersects the surface with a ray
    bool intersect(vec3 origin, vec3 direction, float& z);

    vec3 bbMin,bbMax; //FIXME: normalize to 0-1
    Shader& shader;
    VertexBuffer& vertices;
    array<uint> indices;
    GLIndexBuffer indexBuffer;
};

struct Object {
    Object(Surface& surface) : surface(surface) {}

    Surface& surface;
    vec3 uniformColor {1,1,1};
    mat4 transform;
    vec3 center; vec3 extent; // Bounding box center and extent
    int planeIndex = 0; // Last hit bounding box plane index
};
