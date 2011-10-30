#pragma once
#include "common.h"
#include "image.h"

struct GLUniform {
	GLUniform(int id) : id(id) {}
    void operator=(float);
    void operator=(vec2);
    void operator=(vec4);
	int id;
};
struct GLShader {
	MoveOnly(GLShader);
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
	GLTexture()=default;
	GLTexture(const Image& image);
	operator bool() { return id; }
    void bind();
    void free();
	uint id=0;
};

void glQuad(GLShader& shader, vec2 min, vec2 max, bool texCoord=false);
