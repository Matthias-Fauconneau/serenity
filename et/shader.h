#pragma once
#include "gl.h"
#include "file.h"

extern Folder data;

struct Texture {
    Texture(const string& path=""_/*, string type="albedo"_*/) : path(path), type("albedo"_) {}
    void upload();

    String path;
    String type; //FIXME: convert to flags
    GLTexture* texture = 0;
    bool alpha = false;
    vec3 tcScale {1,1,1/*.0/16*/};
    vec3 rgbScale {1,1,1};
    //string heightMap;
    //bool inverted = true;
};
inline String str(const Texture& o) { return str(o.path, o.type); }

struct Shader : array<Texture> {
    Shader(string type="transform surface"_): type(type) {}
    GLShader* bind();

    String name;
    String type;
    GLShader* program = 0;
    bool polygonOffset=false, alphaTest=false, blendAdd=false, blendAlpha=false, tangentSpace=false, vertexBlend=false;
    String file; int firstLine=0, lastLine=0; String source;
    map<string,String> properties;
};
inline String str(const Shader& o) { return str(o.name,o.type,(ref<Texture>)o); }
