#pragma once
#include "gl.h"

struct FontMetrics {
	long descender;
	long ascender;
	long height;
};

struct GlyphMetrics {
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
	FontMetrics metrics(int size);
	int kerning(char leftCode, char rightCode);
	GlyphMetrics metrics(int size, char code);
	Glyph& glyph(int size, char code);
};

extern Font defaultFont;
