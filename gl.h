#pragma once
#include "string.h"
#include "vector.h"
#include "matrix.h"
#include "map.h"
#include "image.h"

void glCullFace(bool enable);
void glDepthTest(bool enable);
void glBlend(bool enable, bool add=true);

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
    GLShader(const ref<byte>& source, const ref<byte>& tags=""_);
    move_operator(GLShader): id(o.id), sampler2D(move(o.sampler2D)) { o.id=0; }
    void compile(uint type, const ref<byte>& source);
    void bind();
    uint attribLocation(const ref<byte>&);
    GLUniform operator[](const ref<byte>&);

    uint id=0;
    //using pointer comparison (only works with string literals)
    map<string, int> attribLocations;
    map<string, int> uniformLocations;
    array<string> sampler2D; // names of declared sampler2D
};

#define SHADER( name ) \
extern char _binary_ ## name ##_glsl_start[]; \
extern char _binary_ ## name ##_glsl_end[]; \
static ref<byte> name (_binary_ ## name ##_glsl_start,_binary_ ## name ##_glsl_end);

enum PrimitiveType { Point, Line, LineLoop, LineStrip, Triangle, TriangleStrip, TriangleFan, Quad };

struct GLVertexBuffer {
    GLVertexBuffer(){}
    move_operator(GLVertexBuffer): vertexBuffer(o.vertexBuffer),vertexCount(o.vertexCount),vertexSize(o.vertexSize){o.vertexBuffer=0;}
    ~GLVertexBuffer();

    void allocate(int vertexCount, int vertexSize);
    void* mapVertexBuffer();
    void unmapVertexBuffer();
    void upload(const ref<byte>& vertices);
    template<class T> void upload(const ref<T>& vertices) { vertexSize=sizeof(T); upload(ref<byte>((byte*)vertices.data,vertices.size*sizeof(T))); }
    void bindAttribute(GLShader& program, const ref<byte>& name, int elementSize, uint64 offset = 0) const;
    void draw(PrimitiveType primitiveType) const;

    operator bool() const { return vertexBuffer; }

    uint32 vertexBuffer=0;
    uint32 vertexCount=0;
    uint32 vertexSize=0;
};

struct GLIndexBuffer {
    GLIndexBuffer(){}
    GLIndexBuffer(PrimitiveType primitiveType):primitiveType(primitiveType){}
    move_operator(GLIndexBuffer): primitiveType(o.primitiveType),
        indexBuffer(o.indexBuffer),indexCount(o.indexCount),primitiveRestart(o.primitiveRestart) {o.indexBuffer=0;}
    ~GLIndexBuffer();

    void allocate(int indexCount);
    uint* mapIndexBuffer();
    void unmapIndexBuffer();
    void upload(const ref<uint>& indices);
    void draw() const;

    operator bool() { return indexBuffer; }

    PrimitiveType primitiveType=Triangle;
    uint32 indexBuffer=0;
    uint32 indexCount=0;
    bool primitiveRestart=false;
};

vec2 project(vec2 p);
void glDrawRectangle(GLShader& shader, vec2 min=vec2(-1,-1), vec2 max=vec2(1,1), bool texCoord=false);
void glDrawRectangle(GLShader& shader, Rect rect, bool texCoord=false);
void glDrawLine(GLShader& shader, vec2 p1, vec2 p2);

struct GLTexture {
    uint id=0;
    uint width=0, height=0;
    uint format;
    GLTexture(){}
    enum Format {
        sRGB=0,sRGBA=1,Depth24=2,RGB16F=3,
        Mipmap=1<<2, Shadow=1<<3, Bilinear=1<<4, Anisotropic=1<<5, Clamp=1<<6 };
    GLTexture(int width, int height, uint format=sRGB, const void* data=0);
    GLTexture(const Image& image, uint format=sRGB);
    move_operator(GLTexture):id(o.id),width(o.width),height(o.height){o.id=0;}
    ~GLTexture();

    void bind(uint sampler=0) const;

    operator bool() const { return id; }
    int2 size() const { return int2(width,height); }
};

struct GLFrameBuffer {
    move_operator(GLFrameBuffer):
        id(o.id),depthBuffer(o.depthBuffer),colorBuffer(o.colorBuffer),width(o.width),height(o.height),depthTexture(move(o.depthTexture))
    {o.id=o.depthBuffer=o.colorBuffer=0;}
    GLFrameBuffer(){}
    GLFrameBuffer(GLTexture&& depth);
    GLFrameBuffer(uint width, uint height);
    ~GLFrameBuffer();

    operator bool() const { return id; }
    void bind(bool clear=false, vec4 color=1);
    static void bindWindow(int2 position, int2 size, bool clear=false, vec4 color=1);
    void blit(GLTexture&);

    uint id=0, depthBuffer=0, colorBuffer=0;
    uint width=0, height=0;
    GLTexture depthTexture;
};
