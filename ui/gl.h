#pragma once
#include "string.h"
#include "vector.h"
#include "matrix.h"
#include "map.h"
#include "image.h"

void glFramebufferSRGB(bool enable);
void glCullFace(bool enable);
void glDepthTest(bool enable);
void glAlphaTest(bool enable);
void glBlendAdd();
void glBlendAlpha();
void glBlendNone();

struct GLUniform {
    GLUniform(int program, int location) : program(program), location(location) {}
    explicit operator bool() { return location>=0; }
    void operator=(int);
    void operator=(float);
    void operator=(vec2);
    void operator=(vec3);
    void operator=(vec4);
    void operator=(mat3);
    void operator=(mat4);
    int program, location;
};

struct GLShader {
    GLShader(){}
    GLShader(const string& source, const string& tags):GLShader(source, ref<string>{tags}){}
    GLShader(const string& source, const ref<string>& stages={""_});
    void compile(uint type, const string& source);
    void bind();
    void bindSamplers(const ref<string>& textures);
    void bindFragments(const ref<string>& fragments);
    uint attribLocation(const string&);
    GLUniform operator[](const string&);
    //static uint maxUniformBlockSize();

    handle<uint> id;
    //using pointer comparison (only works with string literals)
    map<String, int> attribLocations;
    map<String, int> uniformLocations;
    array<String> sampler2D; // names of declared sampler2D
    array<String> source;
};

enum PrimitiveType { Point, Line, LineLoop, LineStrip, Triangle, TriangleStrip, TriangleFan, Quad };
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

    operator bool() const { return vertexBuffer; }

    handle<uint> vertexBuffer;
    uint vertexCount=0;
    uint vertexSize=0;
};

struct GLIndexBuffer {
    GLIndexBuffer(){}
    default_move(GLIndexBuffer);
    GLIndexBuffer(PrimitiveType primitiveType):primitiveType(primitiveType){}
    ~GLIndexBuffer();

    void allocate(int indexCount);
    uint* mapIndexBuffer();
    void unmapIndexBuffer();
    void upload(const ref<uint16>& indices);
    void upload(const ref<uint>& indices);
    void draw() const;

    operator bool() { return indexBuffer; }

    PrimitiveType primitiveType=Triangle;
    handle<uint> indexBuffer;
    uint indexCount=0;
    uint indexSize=0;
    bool primitiveRestart=false;
};

vec2 project(vec2 p);
void glDrawRectangle(GLShader& shader, vec2 min=vec2(-1,-1), vec2 max=vec2(1,1), bool texCoord=false);
void glDrawRectangle(GLShader& shader, Rect rect, bool texCoord=false);
void glDrawLine(GLShader& shader, vec2 p1, vec2 p2);

enum Format { sRGB8=0,sRGBA=1,Depth24=2,RGB16F=3, Mipmap=1<<2, Shadow=1<<3, Bilinear=1<<4, Anisotropic=1<<5, Clamp=1<<6 };
struct GLTexture {
    handle<uint> id;
    uint width=0, height=0;
    uint format;

    GLTexture(){}
    default_move(GLTexture);
    GLTexture(int width, int height, uint format=sRGB8, const void* data=0);
    GLTexture(const Image& image, uint format=sRGB8);
    ~GLTexture();

    void bind(uint sampler=0) const;
    operator bool() const { return id; }
    int2 size() const { return int2(width,height); }
};

enum { ClearDepth=0x100, ClearColor=0x4000 };
struct GLFrameBuffer {
    GLFrameBuffer(){}
    GLFrameBuffer(GLTexture&& depth);
    GLFrameBuffer(uint width, uint height, uint format=sRGB8, int sampleCount=0);
    ~GLFrameBuffer();

    operator bool() const { return id; }
    void bind(uint clearFlags=0, vec4 color=1);
    static void bindWindow(int2 position, int2 size, uint clearFlags=ClearDepth|ClearColor, vec4 color=1);
    void blit(uint target);
    void blit(GLTexture&);

    handle<uint> id, depthBuffer, colorBuffer;
    uint width=0, height=0;
    GLTexture depthTexture;
};
