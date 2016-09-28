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
 vec2 origin, size;
 bgr3f color = black; float opacity = 1;
 Fill(vec2 origin, vec2 size, bgr3f color = black, float opacity = 1) : origin(origin), size(size), color(color), opacity(opacity) {}
};

/// Image graphic element
struct Blit {
 vec2 origin, size;
 Image image;
 bgr3f color = white; float opacity = 1;
 Blit(vec2 origin, vec2 size, Image&& image, bgr3f color = white, float opacity = 1) : origin(origin), size(size), image(move(image)), color(color), opacity(opacity) {}
};

/// Text graphic element
struct Glyph {
 vec2 origin;
 float fontSize;
 FontData& font;
 uint code;
 uint index;
 bgr3f color = black;
 float opacity = 1;
 bool hint = false;
 Glyph(vec2 origin, float fontSize, FontData& font, uint code, uint index, bgr3f color = black, float opacity = 1, bool hint = false)
  : origin(origin), fontSize(fontSize), font(font), code(code), index(index), color(color), opacity(opacity), hint(hint) {} //req C++14
};

/// Line graphic element
struct Line {
 vec2 a, b;
 bgr3f color = black;
 float opacity = 1;
 bool hint = false;
 Line(vec2 a, vec2 b, bgr3f color = black, float opacity = 1, bool hint = false) : a(a), b(b), color(color), opacity(opacity), hint(hint) {} //req C++14
};

/// Parallelogram graphic element
struct TrapezoidX {
 struct Span { float y, min, max; } span[2];
 bgr3f color = black; float opacity = 1;
 TrapezoidX(Span a, Span b, bgr3f color=black, float opacity=1) : span{a,b}, color(color), opacity(opacity) {}
};

/// Parallelogram graphic element
struct TrapezoidY {
 struct Span { float x, min, max; } span[2];
 bgr3f color = black; float opacity = 1;
 TrapezoidY(Span a, Span b, bgr3f color=black, float opacity=1) : span{a,b}, color(color), opacity(opacity) {}
};

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
 void extend(vec2 p) { min=::min(min, p); max=::max(max, p); }
};
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min), min(a.max,b.max)); }
//inline String str(const Rect& r) { return "["_+str(r.min)+" - "_+str(r.max)+"]"_; }
inline Rect operator +(vec2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }

/// Set of graphic elements
struct Graphics : shareable {
 vec2 offset = 0;
 Rect bounds = Rect(__builtin_inff(), -__builtin_inff()); // bounding box of untransformed primitives
 array<Fill> fills;
 array<Blit> blits;
 array<Glyph> glyphs;
 array<Line> lines;
 array<TrapezoidX> trapezoidsX;
 array<TrapezoidY> trapezoidsY;
 array<Cubic> cubics;

 map<vec2, shared<Graphics>> graphics;

 virtual ~Graphics() {}

 void translate(vec2 offset) {
  assert_(isNumber(offset));
  bounds = offset+bounds;
  for(auto& o: fills) o.origin += offset;
  for(auto& o: blits) o.origin += offset;
  for(auto& o: glyphs) o.origin += offset;
  for(auto& o: trapezoidsX) for(auto& span: o.span) { span.y+=offset.y; span.min+=offset.x; span.max+=offset.x; }
  for(auto& o: trapezoidsY) for(auto& span: o.span) { span.x+=offset.x; span.min+=offset.y; span.max+=offset.y; }
  for(auto& o: lines) { o.a+=offset; o.b+=offset; }
  for(auto& o: cubics) for(vec2& p: o.points) p+=vec2(offset);
 }
 void append(const Graphics& o) {
  bounds.extend(o.bounds.min); bounds.extend(o.bounds.max);
  fills.append(o.fills);
  blits.append(o.blits);
  glyphs.append(o.glyphs);
  trapezoidsX.append(o.trapezoidsX);
  trapezoidsY.append(o.trapezoidsY);
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

/*inline String str(const Graphics& o) {
 return str(o.bounds, o.fills.size, o.blits.size, o.glyphs.size, o.lines.size, o.trapezoidsX.size, o.trapezoidsY.size, o.cubics.size, o.graphics.size());
}*/
