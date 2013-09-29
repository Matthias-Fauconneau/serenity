#pragma once
struct Folder;
#include "gl.h"
#include "matrix.h"

unique<GLTexture> upload(const ref<byte>& file);

struct Texture { // ->Stage
    Texture(const string& path=""_) : path(path), type("color"_) {}

    String path;
    String type; // FIXME: convert to flags
    GLTexture* texture = 0;
    bool alpha = false, clamp = false;
    mat3x2 tcMod;
    vec3 rgbGen {1,1,1};
};
inline String str(const Texture& o) { return "Texture("_+str(o.path, o.type)+")"_; }
inline Texture copy(const Texture& o) {
    Texture t;
    t.path = copy(o.path), t.type = copy(o.type), t.alpha = o.alpha, t.clamp=o.clamp; t.tcMod=o.tcMod, t.rgbGen=o.rgbGen;
    return t;
}
struct Shader : array<Texture> {
    Shader(): type("transform surface position"_), final("fog"_) {}
    Shader(const string& type): type(type) {}
    GLShader& bind();

    String name;
    String type, final;
    GLShader* program = 0;
    bool doubleSided=false, polygonOffset=false, blendAlpha=false, blendColor=false, vertexBlend=false, skyBox=false; // FIXME: bitfield
    String source;
    map<String,String> properties;
};
inline String str(const Shader& o) { return str(o.name,o.type,o.size,(ref<Texture>)o)+"\n"_+o.source; }
inline Shader copy(const Shader& o) {
    Shader t; t.type=copy(o.type), t.final=copy(o.final); t.name=copy(o.name);
    t.doubleSided=o.doubleSided, t.polygonOffset=o.polygonOffset, t.blendAlpha=o.blendAlpha, t.blendColor=o.blendColor;
    t.vertexBlend=o.vertexBlend, t.skyBox=o.skyBox;
    t.source=copy(o.source), t.properties=copy(o.properties);
    t << (ref<Texture>)o;
    return t;
}
