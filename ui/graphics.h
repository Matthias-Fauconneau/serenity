#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "image.h"
#include "font.h"

/// Primary colors
static constexpr bgr3f black (0, 0, 0);
static constexpr bgr3f red (0, 0, 1);
static constexpr bgr3f green (0, 1, 0);
static constexpr bgr3f blue (1, 0, 0);
static constexpr bgr3f white (1, 1, 1);

/// Fill graphic element
struct Fill {
    vec2 origin, size;
	bgr3f color = black; float opacity = 1;
};

/// Image graphic element
struct Blit {
    vec2 origin, size;
    Image image;
	bgr3f color = black; float opacity = 1;
};

/// Text graphic element
struct Glyph {
    vec2 origin;
    Font& font;
	uint index;
	bgr3f color = black; float opacity = 1;
};

/// Line graphic element
struct Line {
    vec2 a, b;
	bgr3f color = black; float opacity = 1;
};

/// Parallelogram graphic element
struct Parallelogram {
	vec2 min,max;
	float dy;
	bgr3f color = black; float opacity = 1;
};

struct Cubic {
	buffer<vec2> points;
	bgr3f color = black; float opacity = 1;
};

/// Set of graphic elements
struct Graphics : shareable {
	vec2 offset;
    array<Fill> fills;
    array<Blit> blits;
    array<Glyph> glyphs;
    array<Line> lines;
	array<Parallelogram> parallelograms;
	array<Cubic> cubics;
	map<vec2, shared<Graphics>> graphics;
	//explicit operator bool() const { return fills || blits || glyphs || lines || parallelograms || cubics || graphics; }
	/*void append(const Graphics& o, vec2 offset) {
        for(auto e: o.fills) { e.origin += offset; fills.append(e); }
        for(const auto& e: o.blits) blits.append(e.origin+offset, e.size, share(e.image));
        for(auto e: o.glyphs) { e.origin += offset; glyphs.append(e); }
        for(auto e: o.lines) { e.a += offset; e.b += offset; lines.append(e); }
		for(auto e: o.parallelograms) { e.min += offset; e.max += offset; parallelograms.append(e); }
	}*/
};
/*inline Graphics copy(const Graphics& o) {
	return {copy(o.fills), copy(o.blits), copy(o.glyphs), copy(o.lines), copy(o.parallelograms), copy(o.cubics), copy(o.graphics)};
}*/
/*inline String str(const Graphics& o) {
	return str(o.fills.size, o.blits.size, o.glyphs.size, o.lines.size, o.parallelograms.size, o.cubics.size, o.graphics.size);
}*/
