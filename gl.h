#pragma once
#include "string.h"
#include "vector.h"
#include "matrix.h"
#include "map.h"
#include "image.h"
#include "window.h"

void glCullFace(bool enable);
void glDepthTest(bool enable);
void glBlend(bool enable, bool add=true);

struct GLUniform {
    GLUniform(int program, int location) : program(program), location(location) {}
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
    GLShader(const ref<byte>& vertex, const ref<byte>& fragment);
    void compile(uint type, const ref<byte>& source);
    void bind();
    uint attribLocation(const char*);
    GLUniform operator[](const char*);
    void bindSamplers(const char* tex0);

    uint id=0;
    //using pointer comparison (only works with string literals)
    map<const char*,int> attribLocations;
    map<const char*,int> uniformLocations;
};

#define SHADER(name) \
GLShader& name ## Shader() { \
    extern char _binary_## name ##_vert_start[]; \
    extern char _binary_## name ##_vert_end[]; \
    extern char _binary_## name ##_frag_start[]; \
    extern char _binary_## name ##_frag_end[]; \
    static GLShader shader = GLShader( ref<byte>(_binary_## name ##_vert_start,_binary_## name ##_vert_end), \
    ref<byte>(_binary_## name ##_frag_start,_binary_## name ##_frag_end)); \
    return shader; \
}

enum PrimitiveType { Point, Line, LineLoop, LineStrip, Triangle, TriangleStrip, TriangleFan, Quad };
struct GLBuffer {
    GLBuffer(){}
    GLBuffer(PrimitiveType primitiveType):primitiveType(primitiveType){}
    move_operator(GLBuffer): primitiveType(o.primitiveType),
        vertexBuffer(o.vertexBuffer),vertexCount(o.vertexCount),vertexSize(o.vertexSize),
        indexBuffer(o.indexBuffer),indexCount(o.indexCount),primitiveRestart(o.primitiveRestart) {o.vertexBuffer=o.indexBuffer=0;}
    void allocate(int indexCount, int vertexCount, int vertexSize);
    uint* mapIndexBuffer();
    void unmapIndexBuffer();
    void* mapVertexBuffer();
    void unmapVertexBuffer();
    void upload(const ref<uint>& indices);
    void upload(const ref<byte>& vertices);
    template<class T> void upload(const ref<T>& vertices) {
        vertexSize=sizeof(T);
        upload(ref<byte>((byte*)vertices.data,vertices.size*sizeof(T)));
    }
    void bind();
    void bindAttribute(GLShader& program, const char* name, int elementSize, uint64 offset = 0);
    void draw();
    ~GLBuffer();

    operator bool() { return vertexBuffer; }

    PrimitiveType primitiveType=Triangle;
    uint32 vertexBuffer=0;
    uint32 vertexCount=0;
    uint32 vertexSize=0;
    uint32 indexBuffer=0;
    uint32 indexCount=0;
    bool primitiveRestart=false;
};

vec2 project(vec2 p);
void glDrawRectangle(GLShader& shader, vec2 min, vec2 max, bool texCoord=false);
void glDrawRectangle(GLShader& shader, Rect rect, bool texCoord=false);
void glDrawLine(GLShader& shader, vec2 p1, vec2 p2);

struct GLTexture {
    uint id=0;
    uint width=0, height=0;
    bool alpha=false;
    GLTexture(){}
    enum Format {
        sRGB=0,Depth24=1,RGB16F=2,
        Mipmap=1<<2, Shadow=1<<3, Bilinear=1<<4, Anisotropic=1<<5, Clamp=1<<6 };
    GLTexture(int width,int height,int format=sRGB);
    GLTexture(const Image& image);
    move_operator(GLTexture):id(o.id),width(o.width),height(o.height){o.id=0;}
    ~GLTexture();

    void bind(uint sampler=0) const;
    static void bindSamplers(const GLTexture& tex0);

    operator bool() const { return id; }
    int2 size() const { return int2(width,height); }
};

struct GLFrameBuffer {
    move_operator(GLFrameBuffer):id(o.id),depthBuffer(o.depthBuffer),colorBuffer(o.colorBuffer),width(o.width),height(o.height)
    { o.id=o.depthBuffer=o.colorBuffer=0;}
    GLFrameBuffer(){}
    GLFrameBuffer(uint width, uint height);
    ~GLFrameBuffer();

    operator bool() const { return id; }
    void bind(bool clear=false, vec4 color=1);
    static void bindWindow(int2 position, int2 size, bool clear=false, vec4 color=1);
    void blit(GLTexture&);

    uint id=0, depthBuffer=0, colorBuffer=0;
    uint width=0, height=0;
};
