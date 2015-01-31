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
	//array<String> sampler2D;
	array<String> source;
};

struct GLBuffer {
	handle<uint> id;
	uint elementSize = 0;
	uint elementCount = 0;

	GLBuffer() {}
	GLBuffer(uint elementSize, ref<byte> data);
	template<Type T> explicit GLBuffer(ref<T> data) : GLBuffer(sizeof(T), cast<byte>(data)) {}
	default_move(GLBuffer);
	~GLBuffer();
	void bind() const;
};

enum PrimitiveType { Point, Lines, LineLoop, LineStrip, Triangles, TriangleStrip };
enum AttributeType { Byte=0x1400, UByte, Short, UShort, Int, UInt, Float };
struct GLVertexArray {
	handle<uint> id;

	GLVertexArray();
	default_move(GLVertexArray);
	~GLVertexArray();

	void bindAttribute(int index, int elementSize, AttributeType type, const GLBuffer& buffer, uint64 offset = 0) const;
	void bind() const;
	void draw(PrimitiveType primitiveType, uint vertexCount) const;
};

struct GLIndexBuffer : GLBuffer {
	template<Type T> GLIndexBuffer(ref<T> data) : GLBuffer(sizeof(T), cast<byte>(data)) {}
	PrimitiveType primitiveType = TriangleStrip;
	void draw(int base);
	//void draw(int instanceCount=1);
};

enum Format { RGB8=0, R16I=1, Depth=2/*U32*/, RGBA8=3, R32F=4,
			  SRGB=1<<3, Mipmap=1<<4, Shadow=1<<5, Bilinear=1<<6, Anisotropic=1<<7, Clamp=1<<8, Multisample=1<<9, Cube=1<<10 };
struct GLTexture {
    handle<uint> id = 0;
	union { int2 size = 0; struct { uint width, height; }; };
	uint format, target;

    GLTexture(){}
    default_move(GLTexture);
    GLTexture(uint width, uint height, uint format=0, const void* data=0);
	GLTexture(const struct Image& image, uint format=0);
	//GLTexture(const struct Image16& image, uint format=0);
	GLTexture(const GLBuffer& buffer, uint format=R32F);
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
	GLFrameBuffer(int2 size, int sampleCount=0/*, uint format=0*/);
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
