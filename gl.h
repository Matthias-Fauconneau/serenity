#pragma once
#include "string.h"
#include "vector.h"
#include "matrix.h"
#include "map.h"
#include "image.h"

extern vec2 viewport;
void glViewport(int2 size);

#define SHADER(name) \
extern char _binary_## name ##_gpu_start[]; \
extern char _binary_## name ##_gpu_end[]; \
GLShader name = array<byte>(_binary_## name ##_gpu_start,_binary_## name ##_gpu_end)

struct GLUniform {
    GLUniform(int id) : id(id) {}
    void operator=(float);
    void operator=(vec2);
    void operator=(vec4);
    void operator=(mat4);
    int id;
};
struct GLShader {
    GLShader(array<byte>&& binary):binary(move(binary)){}
    void bind();
    uint attribLocation(const char*);
    GLUniform operator[](const char*);

    array<byte> binary;
    uint id=0;
    //using pointer comparison (only works with string literals)
    map<const char*,int> attribLocations;
    map<const char*,int> uniformLocations;
};
extern GLShader flat;
extern GLShader blit;

struct GLTexture : Image {
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
    no_copy(GLBuffer)
    GLBuffer(PrimitiveType primitiveType=Point);
    void allocate(int indexCount, int vertexCount, int vertexSize);
    uint* mapIndexBuffer();
    void unmapIndexBuffer();
    void* mapVertexBuffer();
    void unmapVertexBuffer();
    void upload(const array<int>& indices);
    void upload(const array<byte>& vertices);
    template<class T> void upload(const array<T>& vertices) { vertexSize=sizeof(T); upload(array<byte>((byte*)vertices.data,vertices.size*sizeof(T))); }
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
void glWireframe(bool wireframe=true);
