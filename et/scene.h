#pragma once
#include "string.h"
#include "map.h"
#include "vector.h"
struct Folder;
#include "gl.h"
#include "object.h"
struct BSP;

typedef map<String,String> Entity;

struct Scene {
    Scene(string file, const Folder& data);

    array<String> search(const string& query, const string& type);
    /// Gets or creates a shader ensuring correct lighting method (map, grid or dynamic)
    Shader& getShader(string name, int lightmap=-2);
    array<Surface> importBSP(const BSP& bsp, int firstFace, int numFaces, bool leaf);
    array<Surface> importMD3(string modelPath);
    /// Default position as (x, y, z, yaw angle)
    vec4 defaultPosition() const;

    const Folder& data;
    String name;
    map<String,Entity> entities;
    map<String,Entity> targets;
    map<String,unique<Shader>> shaders; // Referenced by Surfaces
    VertexBuffer vertices; // Referenced by Surfaces
    map<String, array<Surface>> models;
    Shader* sky = 0;
    vec4 fog; // RGB, distance
    vec3 gridMin, gridMax; GLTexture lightGrid[3]; // Light grid
    map<GLShader*,array<Object>> opaque, blendAlpha, blendColor; // Objects splitted by renderer state and indexed by GL Shader (to minimize context switches)
    array<Object*> objects; // For hit tests
    map<String,unique<GLTexture>> textures;
};
