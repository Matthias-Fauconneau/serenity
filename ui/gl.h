#pragma once
#include "string.h"
#include "vector.h"
#include "map.h"
struct mat3x2; struct mat3; struct mat4;
struct Image;

extern "C" void glDepthMask(uint8 enable);
void glCullFace(bool enable);
void glDepthTest(bool enable);
void glPolygonOffsetFill(bool enable);
void glBlendAlpha();
void glBlendColor();
void glBlendNone();

struct GLUniform {
    GLUniform(int program, int location) : program(program), location(location) {}
    explicit operator bool() { return location>=0; }
    void operator=(int);
    void operator=(float);
    void operator=(vec2);
    void operator=(vec3);
    void operator=(vec4);
    void operator=(mat3x2);
    void operator=(mat3);
    void operator=(mat4);
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

    operator bool() const { return id; }

    handle<uint> id = 0;
    uint vertexCount = 0;
    uint vertexSize = 0;
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

    operator bool() { return id; }

    PrimitiveType primitiveType=Triangle;
    handle<uint> id = 0;
    uint indexCount=0;
    uint indexSize=0;
    bool primitiveRestart=false;
};

enum Format { sRGB8=0,sRGBA=1,Depth24=2,RGB16F=3,
              Mipmap=1<<2, Shadow=1<<3, Bilinear=1<<4, Anisotropic=1<<5, Clamp=1<<6, Multisample=1<<7 };
struct GLTexture {
    handle<uint> id = 0;
    uint width=0, height=0, depth=0;
    uint format;

    GLTexture(){}
    default_move(GLTexture);
    GLTexture(uint width, uint height, uint format=sRGB8, const void* data=0);
    GLTexture(const Image& image, uint format=sRGB8);
    GLTexture(uint width, uint height, uint depth, const ref<byte4>& data);
    ~GLTexture();

    void bind(uint sampler=0) const;
    operator bool() const { return id; }
    int2 size() const { return int2(width,height); }
};

enum { ClearDepth=0x100, ClearColor=0x4000 };
struct GLFrameBuffer {
    GLFrameBuffer(){}
    default_move(GLFrameBuffer);
    //GLFrameBuffer(GLTexture&& depth);
    //GLFrameBuffer(GLTexture&& depth, GLTexture&& color);
    GLFrameBuffer(uint width, uint height, int sampleCount=0, uint format=sRGB8);
    ~GLFrameBuffer();

    void bind(uint clearFlags=0, vec4 color=1);
    static void bindWindow(int2 position, int2 size, uint clearFlags=0, vec4 color=1);
    void blit(uint target);
    void blit(GLTexture&);
    operator bool() const { return id; }
    int2 size() const { return int2(width,height); }

    handle<uint> id = 0, depthBuffer = 0, colorBuffer = 0;
    uint width = 0, height = 0;
    GLTexture depthTexture, colorTexture;
};
