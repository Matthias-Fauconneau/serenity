#pragma once
#include "string.h"
#include "vector.h"
#include "map.h"
#include "thread.h"
#include "image.h"

void glCullFace(bool enable);
void glDepthTest(bool enable);
void glLine(bool enable);
//void glBlendAlpha();

struct GLUniform {
 GLUniform(uint program, int location) : program(program), location(location) {}
 explicit operator bool() { return location != -1; }
 void operator=(int);
 void operator=(float);
 void operator=(ref<float>);
 void operator=(vec2);
 void operator=(vec3);
 void operator=(ref<vec4>);
 void operator=(vec4);
 void operator=(struct mat3);
 void operator=(struct mat4);
 uint program; int location;
};

struct GLShader {
 GLShader(){}
 GLShader(string source, ref<string> stages={""_});
 void bind() const;
 void bindFragments(ref<string> fragments) const;
 uint attribLocation(string) const;
 GLUniform operator[](string) const;
 GLUniform uniform(string s) const { return (*this)[s]; }
 void bind(string name, const struct GLBuffer& ssbo, uint binding=0) const;

 handle<uint> id = 0;
 //map<String, int> attribLocations;
 //map<String, int> uniformLocations;
 //array<String> source;
};

struct GLUniformBuffer {
 handle<uint> id;
 size_t elementSize = 0;
 size_t elementCount = 0;

 GLUniformBuffer() {}
 template<Type T> GLUniformBuffer(ref<T> data) { upload(data); }
 void upload(uint elementSize, ref<byte> data);
 template<Type T> void upload(ref<T> data) { upload(sizeof(T), cast<byte>(data)); }
 default_move(GLUniformBuffer);
 ~GLUniformBuffer();

 explicit operator bool() { return id; }
};

struct GLBuffer {
 handle<uint> id;
 size_t elementSize = 0;
 size_t elementCount = 0;

 GLBuffer() {}
 template<Type T> explicit GLBuffer(ref<T> data) { upload(data); }
 default_move(GLBuffer);
 ~GLBuffer();

 explicit operator bool() { return id; }

 void upload(uint elementSize, ref<byte> data);
 template<Type T> void upload(ref<T> data) { upload(sizeof(T), cast<byte>(data)); }

 mref<byte> map();
 template<Type T> mref<T> map() { return mcast<T>(map()); }

 mref<byte> map(uint elementSize, size_t elementCount);
 template<Type T> mref<T> map(size_t elementCount) { return mcast<T>(map(sizeof(T), elementCount)); }

 void unmap();
};

enum PrimitiveType { Point, Lines, LineLoop, LineStrip, Triangles, TriangleStrip };
enum AttributeType { Byte=0x1400, UByte, Short, UShort, Int, UInt, Float };
struct GLVertexArray {
 handle<uint> id;

 GLVertexArray();
 default_move(GLVertexArray);
 ~GLVertexArray();

 void bindAttribute(int index, int elementSize, AttributeType type, const GLBuffer& buffer, uint64 offset = 0, uint stride = 0) const;
 void bind() const;
 void draw(PrimitiveType primitiveType, uint vertexCount) const;
};

struct GLIndexBuffer : GLBuffer {
 GLIndexBuffer() {}
 template<Type T> GLIndexBuffer(ref<T> data) { upload(data); }
 //GLIndexBuffer(ref<uint32> data) { upload(data); }
 PrimitiveType primitiveType = Triangles; //Strip;
 void draw(size_t start = 0, size_t end = 0);
};

enum Format { RGB8=0, R16I=1, Depth24=2, RGBA8=3, R32F=4, RGB32F=5, RGBA32F=6,
              SRGB=1<<3, Mipmap=1<<4, Shadow=1<<5, Bilinear=1<<6, Anisotropic=1<<7,
              Clamp=1<<8, Multisample=1<<9, Cube=1<<10 };
struct GLTexture {
 ::handle<uint> id = 0;

 GLTexture(){}
 default_move(GLTexture);
 GLTexture(uint3 size, uint format=0);
 ~GLTexture();

 uint64 handle() const;
 //void upload(uint2 size, ref<rgb3f> data) const;
 void upload(uint3 size, ref<rgb3f> data) const;
 //void upload(uint2 size, ref<vec4> data) const { return upload(size, cast<rgba4f>(data)); }
 void upload(uint3 size, const GLBuffer& buffer, size_t data, size_t bufferSize) const;

 operator bool() const { return id; }
};

enum { ClearDepth=0x100, ClearColor=0x4000 };
struct GLFrameBuffer {
 GLFrameBuffer(){}
 default_move(GLFrameBuffer);
 GLFrameBuffer(uint2 size, uint sampleCount=1);
 ~GLFrameBuffer();

 void bind(uint clearFlags=0, rgba4f color=1) const;
 void blit(uint target, uint2 targetSize=0, uint2 offset=0) const;
 void readback(const Image& target) const;
 void readback(const Image4f& target) const;
 Image readback() const { Image target(size); readback(target); return target; }
 operator bool() const { return id; }

 handle<uint> id = 0, depthBuffer = 0, colorBuffer = 0;
 union { uint2 size = 0; struct { uint width, height; }; };
};
GLFrameBuffer window(uint2 size=0);
