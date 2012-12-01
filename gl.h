#pragma once
#include "string.h"
#include "vector.h"
#include "matrix.h"
#include "map.h"
#include "image.h"
#include "window.h"

struct GLContext {
    void* display;
    void* surface;
    void* context;
    GLContext(Window&);
    ~GLContext();
    void swapBuffers();
};

void glCullFace(bool enable);
void glDepthTest(bool enable);
void glBlend(bool enable);

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
    void bindSamplers(const char* tex0, const char* tex1=0);

    uint id=0;
    //using pointer comparison (only works with string literals)
    map<const char*,int> attribLocations;
    map<const char*,int> uniformLocations;
};

#define SHADER(name) \
static GLShader name ## Shader() { \
    extern char _binary_## name ##_vert_start[]; \
    extern char _binary_## name ##_vert_end[]; \
    extern char _binary_## name ##_frag_start[]; \
    extern char _binary_## name ##_frag_end[]; \
    return GLShader( ref<byte>(_binary_## name ##_vert_start,_binary_## name ##_vert_end), \
    ref<byte>(_binary_## name ##_frag_start,_binary_## name ##_frag_end)); \
} \
GLShader name = name ## Shader()

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
    void upload(const ref<int>& indices);
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

void glQuad(GLShader& shader, vec2 min, vec2 max, bool texCoord=false);

struct GLTexture {
    uint id=0;
    uint width=0, height=0;
    GLTexture(){}
    enum Format {
        sRGB=0,Depth24=1,RGB16F=2,Multisample=3,
        Mipmap=1<<2, Shadow=1<<3, Bilinear=1<<4, Anisotropic=1<<5, Clamp=1<<6 };
    GLTexture(int width,int height,int format=sRGB);
    GLTexture(const Image& image);
    move_operator(GLTexture):id(o.id),width(o.width),height(o.height){o.id=0;}
    ~GLTexture();

    operator bool() const { return id; }
    void bind(uint sampler=0) const;
    static void bindSamplers(const GLTexture& tex0);
};

struct GLFrameBuffer {
    move_operator(GLFrameBuffer):id(o.id),depthBuffer(o.depthBuffer),depth(move(o.depth)),color(move(o.color)){o.id=o.depthBuffer=0;}
    GLFrameBuffer(){}
    GLFrameBuffer(GLTexture&& color);
    ~GLFrameBuffer();

    operator bool() const { return id; }
    void bind(bool clear=false, vec4 color=1);
    static void bindWindow(int2 position, int2 size);

    uint id=0, depthBuffer=0;
    GLTexture depth, color;
};
