#pragma once
#include "string.h"
#include "file.h"
#include "data.h"
#include "object.h"
struct BSP;
typedef map<String,String> Entity;

struct Scene {
    Scene(string file, const Folder& data);

    void parseMaterialFile(string path);
    array<String> search(const string& query, const string& type);
    array<Surface> importBSP(const BSP& bsp, const ref<Vertex>& vertices, int firstFace, int numFaces, bool leaf);
    array<Surface> importMD3(string modelPath);
    /// Default position as (x, y, z, yaw angle)
    vec4 defaultPosition() const;

    map<String,Entity> entities;
    map<String,Entity> targets;
    map<String,unique<Shader>> shaders; // Referenced by Surfaces
    map<String, array<Surface>> models;
    array<Object> shadowOnly;
    map<GLShader*,array<Object>> opaque, alphaTest, blendAdd, blendAlpha; // Objects splitted by renderer state and indexed by GL Shader (to minimize context switches)
    array<Object*> objects; // For hit tests
    array<Light> lights;
};
