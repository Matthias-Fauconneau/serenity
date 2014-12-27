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
// FIXME: Implement as polygon
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
    float fontSize;
    FontData& font;
	uint code;
	uint index;
	bgr3f color = black; float opacity = 1;
};

/// Line graphic element
struct Line {
    vec2 a, b;
	bgr3f color = black; float opacity = 1;
};

/// Parallelogram graphic element
// FIXME: Implement as polygon
struct Parallelogram {
	vec2 min,max;
	float dy;
	bgr3f color = black; float opacity = 1;
	Parallelogram(vec2 min, vec2 max, float dy, bgr3f color=black, float opacity=1) : min(min), max(max), dy(dy), color(color), opacity(opacity) {}
};

/*/// Polygon graphic element
// FIXME: Implements as cubic path
struct Polygon {
    vec2 min,max; array<Line> edges;
};*/

struct Cubic {
	buffer<vec2> points;
	bgr3f color = black; float opacity = 1;
	Cubic(buffer<vec2>&& points, bgr3f color=black, float opacity=1) : points(move(points)), color(color), opacity(opacity) {}
};

/// Axis-aligned rectangle with 2D floating point coordinates
struct Rect {
    vec2 min, max;
    explicit Rect(vec2 size) : min(0), max(size) {}
    explicit Rect(vec2 min, vec2 max) : min(min), max(max) {}
    static Rect fromOriginAndSize(vec2 origin, vec2 size) { return Rect(origin, origin+size); }
    vec2 origin() const { return min; }
    vec2 size() const { return max-min; }
    explicit operator bool() { return min<max; }
    bool contains(vec2 p) const { return p>=min && p<=max; }
    void extend(vec2 p) { min=::min(min, p), max=::max(max, p); }
};
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min),min(a.max,b.max)); }
inline String str(const Rect& r) { return "["_+str(r.min)+" - "_+str(r.max)+"]"_; }
inline Rect operator +(vec2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }

/// Set of graphic elements
struct Graphics : shareable {
    //vec2 size = 0;
    vec2 offset = 0;
    //vec2 scale = 1; TODO

    Rect bounds = Rect(inf, -inf); // bounding box of untransformed primitives
    array<Fill> fills;
    array<Blit> blits;
    array<Glyph> glyphs;
    array<Line> lines;
    array<Parallelogram> parallelograms;
    //array<Polygon> polygons;
    array<Cubic> cubics;

    map<vec2, shared<Graphics>> graphics;

    void translate(vec2 offset) {
        bounds = offset+bounds;
        for(auto& o: fills) o.origin += offset;
		assert_(!blits);
		for(auto& o: glyphs) o.origin += offset;
        for(auto& o: parallelograms) o.min+=offset, o.max+=offset;
		for(auto& o: lines) o.a+=offset, o.b+=offset;
		for(auto& o: cubics) for(vec2& p: o.points) p+=vec2(offset);
    }
	void append(const Graphics& o) {
        bounds.extend(o.bounds.min); bounds.extend(o.bounds.max);
        fills.append(o.fills);
		assert_(!o.blits);
		glyphs.append(o.glyphs);
        parallelograms.append(o.parallelograms);
		lines.append(o.lines);
		cubics.append(o.cubics);
	}
};

inline String str(const Graphics& o) {
    return str(o.bounds, o.fills.size, o.blits.size, o.glyphs.size, o.lines.size, o.parallelograms.size, o.cubics.size, o.graphics.size());
}
