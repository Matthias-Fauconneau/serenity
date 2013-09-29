#pragma once
#include "shader.h"

struct Vertex {
    vec3 position; float alpha; vec2 texcoord; vec2 lightmap; vec3 normal; // 11
    Vertex(vec3 position,vec2 texcoord,vec3 normal,float alpha=0,vec2 lightmap=0) :
        position(position), alpha(alpha), texcoord(texcoord), lightmap(lightmap), normal(normal) {}
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
    /// Renders the surface using \a program
    void draw(GLShader& program);
    /// Intersects the surface with a ray
    bool intersect(vec3 origin, vec3 direction, float& z);

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
