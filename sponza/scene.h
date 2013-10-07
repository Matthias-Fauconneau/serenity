#pragma once
#include "gl.h"

struct Vertex {
    Vertex(vec3 position, vec2 texCoords, vec3 normal):position(position),texCoords(texCoords),normal(normal){}
    vec3 position;
    vec2 texCoords;
    vec3 normal;
};

struct Material : shareable {
    vec3 diffuse=1;//, specular=0;
    //float transparency=1, specularExponent=0;
    //String colorPath;//, maskPath, normalPath;
    //GLTexture color; // + transparency
    //GLTexture normal; // + displacement
};

struct ptni { int p,t,n,i; };
struct Surface {
    //string name;
    shared<Material> material;
    array<Vertex> vertices;
    array<uint> indices;
    GLVertexBuffer vertexBuffer;
    GLIndexBuffer indexBuffer;
};

struct Scene {
    Scene();

    array<Surface> surfaces;
    vec3 worldMin=0, worldMax=0; // Scene bounding box in world space
};
