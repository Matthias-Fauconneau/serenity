#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "core/image.h"
#include "font.h"

/// Primary colors
static constexpr bgr3f black (0, 0, 0);
static constexpr bgr3f red (0, 0, 1);
static constexpr bgr3f green (0, 1, 0);
static constexpr bgr3f blue (1, 0, 0);
static constexpr bgr3f white (1, 1, 1);
static constexpr bgr3f cyan (1, 1, 0);
static constexpr bgr3f magenta (1, 0, 1);
static constexpr bgr3f yellow (0, 1, 1);

/// Fill graphic element
struct Fill {
    vec2 origin, size;
	bgr3f color = black; float opacity = 1;
};

/// Image graphic element
struct Blit {
    vec2 origin, size;
    Image image;
	bgr3f color = white; float opacity = 1;
};

/// Text graphic element
struct Glyph {
    vec2 origin;
    Font& font;
	uint code;
	uint index;
	bgr3f color = black; float opacity = 1;
	Glyph(vec2 origin, Font& font, uint code, uint index, bgr3f color = black, float opacity=1)
		: origin(origin), font(font), code(code), index(index), color(color), opacity(opacity) {}
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
	Parallelogram(vec2 min, vec2 max, float dy, bgr3f color=black, float opacity=1) : min(min), max(max), dy(dy), color(color), opacity(opacity) {}
};

struct Cubic {
	buffer<vec2> points;
	bgr3f color = black; float opacity = 1;
	Cubic(buffer<vec2>&& points, bgr3f color=black, float opacity=1) : points(move(points)), color(color), opacity(opacity) {}
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

	void translate(vec2 offset) {
		for(auto& o: fills) o.origin += offset;
		assert_(!blits);
		for(auto& o: glyphs) o.origin += offset;
		for(auto& o: parallelograms) o.min+=offset, o.max+=offset;
		assert_(!lines);
		for(auto& o: cubics) for(vec2& p: o.points) p+=vec2(offset);
	}
};

inline String str(const Graphics& o) {
	return str(o.fills.size, o.blits.size, o.glyphs.size, o.lines.size, o.parallelograms.size, o.cubics.size, o.graphics.size());
}
