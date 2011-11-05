#pragma once
#include "string.h"
#include "vector.h"
#include "map.h"
#include "image.h"

struct GLUniform {
	GLUniform(int id) : id(id) {}
    void operator=(float);
    void operator=(vec2);
    void operator=(vec4);
	int id;
};
struct GLShader {
	no_copy(GLShader)
	GLShader(const char* name) : name(name) {}
	bool compileShader(uint id, uint type, const array<string>& tags);
	bool compile(const array<string>& vertex, const array<string>& fragment);
    void bind();
	uint attribLocation(const char*);
    GLUniform operator[](const char*);

	const char* name=0;
	uint id=0;
    //using pointer comparison (only works with string literals)
	map<const char*,int> attribLocations;
	map<const char*,int> uniformLocations;
	//debug:
	string vertex,fragment;
};
extern GLShader flat;
extern GLShader blit;

struct GLTexture : Image {
	move_only(GLTexture)
	GLTexture(){}
	GLTexture(const Image& image);
	operator bool() const { return id; }
	void bind() const;
    void free();
	uint id=0;

	static void bind(int id);
};

struct VertexAttribPointer { uint32 offset; int vertexSize, elementSize; };
struct GLBuffer {
	operator bool() { return vertexBuffer; }
	void upload(const array<uint32>& indices);
	void upload(const array<vec2>& vertices);
	void bind();
	void bindAttribute(GLShader* program, const char* name, int elementSize, uint64 offset = 0);
	void draw();

	uint32 vertexBuffer=0;
	uint32 vertexCount=0;
	uint32 vertexSize=0;
	uint32 indexBuffer=0;
	uint32 indexCount=0;
	uint32 primitiveType=3;
};

void glQuad(GLShader& shader, vec2 min, vec2 max, bool texCoord=false);
