#pragma once
#include "string.h"
#include "vector.h"
#include "map.h"
#include "matrix.h"
#include "volume.h"

struct GLUniform {
    GLUniform(int program, int location) : program(program), location(location) {}
    explicit operator bool() { return location>=0; }
    void operator=(int);
    void operator=(float);
    void operator=(vec2);
    void operator=(vec3);
    void operator=(vec4);
    void operator=(mat3);
    int program, location;
};

struct GLShader {
    GLShader(){}
    GLShader(const string& source, const ref<string>& stages={""_});
    void compile(uint type, const string& source);
    void bind();
    void bindSamplers(const ref<string>& textures);
    void bindFragments(const ref<string>& fragments);
    uint attribLocation(const string&);
    GLUniform operator[](const string&);

    handle<uint> id = 0;
    map<String, int> attribLocations;
    map<String, int> uniformLocations;
    array<String> sampler2D;
    array<String> source;
};

#define SHADER(name) \
    extern char _binary_ ## name ##_glsl_start[], _binary_ ## name ##_glsl_end[]; \
    static GLShader name ## Shader (ref<byte>(_binary_ ## name ##_glsl_start,_binary_ ## name ##_glsl_end)); \

struct GLUniformBuffer {
    GLUniformBuffer(){}
    default_move(GLUniformBuffer);
    ~GLUniformBuffer();

    void upload(const ref<byte>& data);
    template<class T> void upload(const ref<T>& data) { upload(ref<byte>((byte*)data.data,data.size*sizeof(T))); }
    void bind(GLShader& program, const ref<byte>& name) const;

    operator bool() const { return id; }

    handle<uint> id = 0;
    int size = 0;
};

enum PrimitiveType { Point, Lines, LineLoop, LineStrip, Triangles, TriangleStrip };
struct GLVertexBuffer {
    GLVertexBuffer(){}
    default_move(GLVertexBuffer);
    ~GLVertexBuffer();

    void allocate(int vertexCount, int vertexSize);
    void* mapVertexBuffer();
    void unmapVertexBuffer();
    void upload(const ref<byte>& vertices);
    template<Type T> void upload(const ref<T>& vertices) { vertexSize=sizeof(T); upload(ref<byte>((byte*)vertices.data,vertices.size*sizeof(T))); }
    void bindAttribute(GLShader& program, const ref<byte>& name, int elementSize, uint64 offset = 0/*, bool instance = false*/) const;
    void draw(PrimitiveType primitiveType) const;

    operator bool() const { return id; }

    handle<uint> id = 0;
    uint vertexCount = 0;
    uint vertexSize = 0;
};
#undef offsetof
#define offsetof __builtin_offsetof

struct GLTexture {
    handle<uint> id = 0;
    int3 size;

    GLTexture(){}
    default_move(GLTexture);
    GLTexture(int2 size);
    GLTexture(const VolumeF& volume);
    ~GLTexture();

    operator bool() const { return id; }
    void bind(uint sampler=0) const;
    void read(const ImageF& target) const;
};

enum { ClearDepth=0x100, ClearColor=0x4000 };
struct GLFrameBuffer {
    GLFrameBuffer(){}
    default_move(GLFrameBuffer);
    GLFrameBuffer(GLTexture&& texture);
    ~GLFrameBuffer();

    void bind(uint clearFlags=0, vec4 color=1);
    operator bool() const { return id; }
    int2 size() const { return texture.size.xy(); }

    handle<uint> id = 0;
    GLTexture texture;
};
