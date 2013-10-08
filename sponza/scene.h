#pragma once
#include "gl.h"
#include "matrix.h"

struct Vertex {
    Vertex(vec3 position, vec2 texCoords, vec3 normal):position(position),texCoords(texCoords),normal(normal){}
    vec3 position;
    vec2 texCoords;
    vec3 normal;
    vec3 tangent, bitangent;
};

struct Material : shareable {
    Material(const string& name):name(name){}
    String name;
    String diffusePath, maskPath;
    GLTexture diffuse; // + mask
    String normalPath, specularPath;
    GLTexture normal; // + specular (TODO)
};

struct ptni { int p,t,n,i; };
struct Surface {
    Surface(const string& name):name(name){}
    String name;
    shared<Material> material = 0;
    array<Vertex> vertices;
    array<uint> indices;
    GLVertexBuffer vertexBuffer;
    GLIndexBuffer indexBuffer;
};

struct Scene {
    Scene();

    array<Surface> replace, blend;
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space

    mat4 light; // Light projection transform
    vec3 lightMin=0, lightMax=0; // Scene bounding box in light space
};
