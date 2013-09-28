#pragma once
#include "gl.h"
#include "file.h"

extern Folder data;
extern map<String,unique<GLTexture>> textures;

struct Texture {
    Texture(const string& path=""_) : path(path), type("albedo"_) {}
    void upload();

    String path;
    String type; // albedo|displace|tangent|lightmap|lightgrid + shading attributes (FIXME: convert to flags)
    GLTexture* texture = 0;
    bool alpha = false, clamp = false;
    vec3 tcScale {1,1,1/*.0/16*/};
    vec3 rgbScale {1,1,1};
    //string heightMap;
    //bool inverted = true;
};
inline String str(const Texture& o) { return "Texture("_+str(o.path, o.type)+")"_; }
inline Texture copy(const Texture& o) {
    Texture t;
    t.path = copy(o.path), t.type = copy(o.type), t.alpha = o.alpha, t.clamp=o.clamp; t.tcScale=o.tcScale, t.rgbScale=o.rgbScale;
    return t;
}
struct Shader : array<Texture> {
    Shader(string type="transform surface"_): type(type) {}
    GLShader* bind();

    String name;
    String type;
    GLShader* program = 0;
    bool polygonOffset=false, alphaTest=false, blendAdd=false, blendAlpha=false, tangentSpace=false, vertexBlend=false; // FIXME: bitfield
    String file; int firstLine=0, lastLine=0; String source;
    map<string,String> properties;
};
inline String str(const Shader& o) { return str(o.name,o.type,o.size,(ref<Texture>)o)+"\n"_+o.source; }
inline Shader copy(const Shader& o) {
    Shader t(o.type); t.name=copy(o.name);
    t.polygonOffset=o.polygonOffset, t.alphaTest=o.alphaTest, t.blendAdd=o.blendAdd, t.blendAlpha=o.blendAdd, t.tangentSpace=o.tangentSpace, t.vertexBlend=o.vertexBlend; // FIXME: bitfield
    t.file = copy(o.file), t.firstLine=o.firstLine, t.lastLine=o.lastLine, t.source=copy(o.source), t.properties=copy(o.properties);
    t << (ref<Texture>)o;
    return t;
}
