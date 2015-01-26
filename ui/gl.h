#pragma once
#include "string.h"
#include "vector.h"
#include "map.h"

extern "C" void glFlush(void);
extern "C" void glDepthMask(uint8 enable);
void glCullFace(bool enable);
void glDepthTest(bool enable);
void glPolygonOffsetFill(bool enable);
void glBlendNone();
void glBlendAlpha();
void glBlendOneAlpha();
void glBlendColor();
void glBlendSubstract();

struct GLUniform {
    GLUniform(int program, int location) : program(program), location(location) {}
    explicit operator bool() { return location>=0; }
    void operator=(int);
    void operator=(float);
    void operator=(vec2);
    void operator=(vec3);
    void operator=(vec4);
	void operator=(struct mat3x2);
	void operator=(struct mat3);
	void operator=(struct mat4);
    int program, location;
};

struct GLShader {
    GLShader(){}
	GLShader(string source, ref<string> stages={""_});
	void compile(uint type, string source);
    void bind();
	void bindFragments(ref<string> fragments);
	uint attribLocation(string);
	GLUniform operator[](string);

    handle<uint> id = 0;
    map<String, int> attribLocations;
    map<String, int> uniformLocations;
    array<String> sampler2D;
	array<String> source;
};

struct GLUniformBuffer {
    GLUniformBuffer(){}
    default_move(GLUniformBuffer);
    ~GLUniformBuffer();

	void upload(ref<byte> data);
	template<class T> void upload(ref<T> data) { upload(ref<byte>((byte*)data.data,data.size*sizeof(T))); }
	void bind(GLShader& program, string name) const;

    operator bool() const { return id; }

    handle<uint> id = 0;
    int size = 0;
};

enum PrimitiveType { Point, Lines, LineLoop, LineStrip, Triangles, TriangleStrip };
struct GLVertexBuffer {
	GLVertexBuffer();
    default_move(GLVertexBuffer);
    ~GLVertexBuffer();

    void allocate(int vertexCount, int vertexSize);
	void* map();
	void unmap();
	void upload(ref<byte> vertices);
	template<Type T> void upload(ref<T> vertices) { vertexSize=sizeof(T); upload(ref<byte>((byte*)vertices.data,vertices.size*sizeof(T))); }
	void bindAttribute(GLShader& program, string name, int elementSize, uint64 offset = 0/*, bool instance = false*/) const;
    void draw(PrimitiveType primitiveType) const;

	handle<uint> array = 0;
	handle<uint> buffer = 0;
    uint vertexCount = 0;
    uint vertexSize = 0;
};
#undef offsetof
#define offsetof __builtin_offsetof

struct GLIndexBuffer {
    default_move(GLIndexBuffer);
	GLIndexBuffer(PrimitiveType primitiveType = Triangles) : primitiveType(primitiveType) {}
    ~GLIndexBuffer();

    void allocate(int indexCount);
    uint* mapIndexBuffer();
    void unmapIndexBuffer();
	void upload(ref<uint16> indices);
	void upload(ref<uint> indices);
    void draw(uint start=0, uint count=-1) const;

    operator bool() { return id; }

	PrimitiveType primitiveType;
    handle<uint> id = 0;
    uint indexCount=0;
    uint indexSize=0;
	bool primitiveRestart = primitiveType == TriangleStrip;
};

enum Format { Depth=1,
			  Alpha=1<<1, SRGB=1<<2,Mipmap=1<<3, Shadow=1<<4, Bilinear=1<<5, Anisotropic=1<<6, Clamp=1<<7, Multisample=1<<8, Cube=1<<9 };
struct GLTexture {
    handle<uint> id = 0;
	union { int2 size = 0; struct { uint width, height; }; };
	uint format, target;

    GLTexture(){}
    default_move(GLTexture);
    GLTexture(uint width, uint height, uint format=0, const void* data=0);
	GLTexture(const struct Image& image, uint format=0);
	GLTexture(uint width, uint height, uint depth, ref<byte4> data);
    ~GLTexture();

    void bind(uint sampler=0) const;
	operator bool() const { return id; }
};

enum { ClearDepth=0x100, ClearColor=0x4000 };
struct GLFrameBuffer {
    GLFrameBuffer(){}
    default_move(GLFrameBuffer);
    GLFrameBuffer(GLTexture&& depth);
    GLFrameBuffer(GLTexture&& depth, GLTexture&& color);
	GLFrameBuffer(int2 size, int sampleCount=0, uint format=0);
    ~GLFrameBuffer();

    void bind(uint clearFlags=0, vec4 color=1);
    static void bindWindow(int2 position, int2 size, uint clearFlags=0, vec4 color=1);
    void blit(uint target);
    void blit(GLTexture&);
    operator bool() const { return id; }

    handle<uint> id = 0, depthBuffer = 0, colorBuffer = 0;
	union { int2 size = 0; struct { uint width, height; }; };
    GLTexture depthTexture, colorTexture;
};
