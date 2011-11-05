#pragma once
#include "gl.h"

struct Metrics {
	vec2 advance;
	vec2 size;
};

struct Glyph {
	vec2 offset;
	vec2 advance;
	GLTexture texture;
};

struct FT_FaceRec_;
struct Font {
	string name;
	string data;
	FT_FaceRec_* face=0;
	map<int, map<int, Glyph> > cache;
	array<float> widths;

	Font(){}
	Font(const string& path);
	int descender();
	int ascender();
	int height();
	int kerning(char leftCode, char rightCode);
	Metrics metrics(int size, char code);
	Glyph& glyph(int size, char code);
};

extern Font defaultFont;
