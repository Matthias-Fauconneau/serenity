#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "image.h"
#include "font.h"

/// Primary colors
static constexpr bgr3f black {0, 0, 0};
static constexpr bgr3f red {0, 0, 1};
static constexpr bgr3f green {0, 1, 0};
static constexpr bgr3f blue {1, 0, 0};
static constexpr bgr3f white {1, 1, 1};
static constexpr bgr3f cyan {1, 1, 0};
static constexpr bgr3f magenta {1, 0, 1};
static constexpr bgr3f yellow {0, 1, 1};

/// Fill graphic element
// FIXME: Implement as polygon
struct Fill {
    vec2f origin, size;
    bgr3f color = black; float opacity = 1;
    Fill(vec2f origin, vec2f size, bgr3f color = black, float opacity = 1) : origin(origin), size(size), color(color), opacity(opacity) {}
};

/// Image graphic element
struct Blit {
    vec2f origin, size;
    Image image;
    bgr3f color = white; float opacity = 1;
    Blit(vec2f origin, vec2f size, Image&& image, bgr3f color = white, float opacity = 1) : origin(origin), size(size), image(move(image)), color(color), opacity(opacity) {}
};

/// Text graphic element
struct Glyph {
    vec2f origin;
    float fontSize;
    FontData& font;
    uint code;
    uint index;
    bgr3f color = black;
    float opacity = 1;
    bool hint = false;
    Glyph(vec2f origin, float fontSize, FontData& font, uint code, uint index, bgr3f color = black, float opacity = 1, bool hint = false)
        : origin(origin), fontSize(fontSize), font(font), code(code), index(index), color(color), opacity(opacity), hint(hint) {} //req C++14
};

/// Line graphic element
struct Line {
    vec2f a, b;
    bgr3f color = black;
    float opacity = 1;
    bool hint = false;
    Line(vec2f a, vec2f b, bgr3f color = black, float opacity = 1, bool hint = false) : a(a), b(b), color(color), opacity(opacity), hint(hint) {} //req C++14
};

/// Parallelogram graphic element
// FIXME: Implement as polygon
struct Parallelogram {
    vec2f min,max;
    float dy;
    bgr3f color = black; float opacity = 1;
    Parallelogram(vec2f min, vec2f max, float dy, bgr3f color=black, float opacity=1) : min(min), max(max), dy(dy), color(color), opacity(opacity) {}
};

/*/// Polygon graphic element
// FIXME: Implements as cubic path
struct Polygon {
    vec2f min,max; array<Line> edges;
};*/

struct Cubic {
    buffer<vec2f> points;
    bgr3f color = black; float opacity = 1;
    Cubic(buffer<vec2f>&& points, bgr3f color=black, float opacity=1) : points(move(points)), color(color), opacity(opacity) {}
};

/// Axis-aligned rectangle with 2D floating point coordinates
struct Rect {
    vec2f min, max;
    explicit Rect(vec2f size) : min(0), max(size) {}
    explicit Rect(vec2f min, vec2f max) : min(min), max(max) {}
    static Rect fromOriginAndSize(vec2f origin, vec2f size) { return Rect(origin, origin+size); }
    vec2f origin() const { return min; }
    vec2f size() const { return max-min; }
    explicit operator bool() { return min<max; }
    bool contains(vec2f p) const { return p>=min && p<=max; }
 void extend(vec2f p) { min=::min(min, p); max=::max(max, p); }
};
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min),min(a.max,b.max)); }
inline String str(const Rect& r) { return "["_+str(r.min)+" - "_+str(r.max)+"]"_; }
inline Rect operator +(vec2f offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }

/// Set of graphic elements
struct Graphics : shareable {
    vec2f offset = 0;
    Rect bounds = Rect(inf, -inf); // bounding box of untransformed primitives
    array<Fill> fills;
    array<Blit> blits;
    array<Glyph> glyphs;
    array<Line> lines;
    array<Parallelogram> parallelograms;
    array<Cubic> cubics;

    map<vec2f, shared<Graphics>> graphics;

    void translate(vec2f offset) {
        assert_(isNumber(offset));
        bounds = offset+bounds;
        for(auto& o: fills) o.origin += offset;
        for(auto& o: blits) o.origin += offset;
        for(auto& o: glyphs) o.origin += offset;
		for(auto& o: parallelograms) { o.min+=offset; o.max+=offset; }
		for(auto& o: lines) { o.a+=offset; o.b+=offset; }
        for(auto& o: cubics) for(vec2f& p: o.points) p+=vec2f(offset);
    }
    void append(const Graphics& o) {
        bounds.extend(o.bounds.min); bounds.extend(o.bounds.max);
        fills.append(o.fills);
        blits.append(o.blits);
        glyphs.append(o.glyphs);
        parallelograms.append(o.parallelograms);
        lines.append(o.lines);
        cubics.append(o.cubics);
    }
    void flatten() {
        for(auto e: graphics) {
            e.value->flatten();
            e.value->translate(e.key);
            append(e.value);
        }
        graphics.clear();
    }
};

inline String str(const Graphics& o) {
    return str(o.bounds, o.fills.size, o.blits.size, o.glyphs.size, o.lines.size, o.parallelograms.size, o.cubics.size, o.graphics.size());
}
