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

void glViewport(int2 position, int2 size);
void glClear();
void glBlend(bool blend=true);

struct GLUniform {
    GLUniform(int program, int location) : program(program), location(location) {}
    void operator=(float);
    void operator=(vec2);
    void operator=(vec4);
    void operator=(mat4);
    int program, location;
};

struct GLShader {
    GLShader(const ref<byte>& vertex, const ref<byte>& fragment);
    void bind();
    uint attribLocation(const char*);
    GLUniform operator[](const char*);

    uint id=0;
    //using pointer comparison (only works with string literals)
    map<const char*,int> attribLocations;
    map<const char*,int> uniformLocations;
};

#define SHADER(name) \
static GLShader name ## Shader() { \
    extern char _binary_## name ##_v_start[]; \
    extern char _binary_## name ##_v_end[]; \
    extern char _binary_## name ##_f_start[]; \
    extern char _binary_## name ##_f_end[]; \
    return GLShader( ref<byte>(_binary_## name ##_v_start,_binary_## name ##_v_end), \
    ref<byte>(_binary_## name ##_f_start,_binary_## name ##_f_end)); \
} \
GLShader name = name ## Shader()

struct GLTexture {
    GLTexture(){}
    GLTexture(const Image& image);
    operator bool() const { return id; }
    void bind() const;
    void free();
    uint id=0;

    static void bind(int id);
};

enum PrimitiveType { Point, Line, LineLoop, LineStrip, Triangle, TriangleStrip, TriangleFan, Quad };
struct GLBuffer {
    no_copy(GLBuffer);
    GLBuffer(PrimitiveType primitiveType=Triangle);
    void allocate(int indexCount, int vertexCount, int vertexSize);
    uint* mapIndexBuffer();
    void unmapIndexBuffer();
    void* mapVertexBuffer();
    void unmapVertexBuffer();
    void upload(const ref<int>& indices);
    void upload(const ref<byte>& vertices);
    template<class T> void upload(const ref<T>& vertices) { vertexSize=sizeof(T); upload(ref<byte>((byte*)vertices.data,vertices.size*sizeof(T))); }
    void bind();
    void bindAttribute(GLShader& program, const char* name, int elementSize, uint64 offset = 0);
    void draw();
    ~GLBuffer();

    operator bool() { return vertexBuffer; }

    uint32 vertexBuffer=0;
    uint32 vertexCount=0;
    uint32 vertexSize=0;
    uint32 indexBuffer=0;
    uint32 indexCount=0;
    PrimitiveType primitiveType = Point;
    bool primitiveRestart=false;
};

void glQuad(GLShader& shader, vec2 min, vec2 max, bool texCoord=false);
