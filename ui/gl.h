#pragma once
#include "string.h"
#include "vector.h"
#include "map.h"
#include "thread.h"
#include "image.h"

void glCheck_(string message);
#define glCheck(message ...) glCheck_(str(__FILE__, __LINE__, ## message ))

void glCullFace(bool enable);
void glDepthTest(bool enable);
void glAlphaTest(bool enable);

struct GLShader {
    GLShader(){}
	GLShader(string source, ref<string> stages={""_});
	void compile(uint type, string source);
    void bind();
	void bindFragments(ref<string> fragments);
	uint attribLocation(string);
	struct GLUniform operator[](string);

    handle<uint> id = 0;
    map<String, int> attribLocations;
    map<String, int> uniformLocations;
	array<String> source;
};

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

struct GLBuffer {
	handle<uint> id;
	size_t elementSize = 0;
	size_t elementCount = 0;

	GLBuffer() {}
	GLBuffer(uint elementSize, ref<byte> data);
	template<Type T> explicit GLBuffer(ref<T> data) : GLBuffer(sizeof(T), cast<byte>(data)) {}
	default_move(GLBuffer);
	~GLBuffer();

	explicit operator bool() { return id; }
	void unmap();
	generic struct Map : mref<T> { GLBuffer& o; Map(mref<T> b, GLBuffer& o) : mref<T>(b), o(o){} ~Map() { o.unmap(); } };
	void* rawMap();
	generic Map<T> map() { assert_(sizeof(T)==elementSize); return {mref<T>((T*)rawMap(), elementCount), *this}; }
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
	GLIndexBuffer() {}
	template<Type T> GLIndexBuffer(ref<T> data) : GLBuffer(sizeof(T), cast<byte>(data)) {}
	PrimitiveType primitiveType = TriangleStrip;
	void draw(size_t start = 0, size_t end = 0);
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
    GLTexture(const Image& image, uint format=0);
	//GLTexture(const struct Image16& image, uint format=0);
	GLTexture(const GLBuffer& buffer, int2 size, uint format=R32F);
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
    static void bindWindow(int2 position, int2 size, uint clearFlags=0, vec4f color=1);
	static void blitWindow(const GLTexture& source, int2 offset=0);
    void blit(uint target);
    void blit(GLTexture&);
    operator bool() const { return id; }

    handle<uint> id = 0, depthBuffer = 0, colorBuffer = 0;
	union { int2 size = 0; struct { uint width, height; }; };
    GLTexture depthTexture, colorTexture;
};

void glDrawRectangle(GLShader& shader, vec2 min=vec2(-1,-1), vec2 max=vec2(1,1), bool texCoord=false);
