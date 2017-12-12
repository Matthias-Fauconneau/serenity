#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "image.h"
#include "font.h"
#include "rect.h"
#include "math.h"

/// Fill graphic element
// FIXME: Implement as polygon
struct Fill {
 vec2 origin, size;
 bgr3f color = 0; float opacity = 1;
 Fill(vec2 origin, vec2 size, bgr3f color = 0, float opacity = 1) : origin(origin), size(size), color(color), opacity(opacity) {}
};

/// Image graphic element
struct Blit {
 vec2 origin, size;
 Image image;
 bgr3f color = 1; float opacity = 1;
 Blit(vec2 origin, vec2 size, Image&& image, bgr3f color = 1, float opacity = 1)
     : origin(origin), size(size), image(move(image)), color(color), opacity(opacity) {}
};

/// Text graphic element
struct Glyph {
 vec2 origin;
 float fontSize;
 FontData& font;
 uint code;
 uint index;
 bgr3f color = 0;
 float opacity = 1;
 bool hint = false;
 /*Glyph(vec2 origin, float fontSize, FontData& font, uint code, uint index, bgr3f color = 0, float opacity = 1, bool hint = false)
  : origin(origin), fontSize(fontSize), font(font), code(code), index(index), color(color), opacity(opacity), hint(hint) {}*/
};

/// Line graphic element
struct Line {
 vec2 p0, p1;
 bgr3f color = 0;
 float opacity = 1;
 bool hint = false;
 //Line(vec2 a, vec2 b, bgr3f color = 0, float opacity = 1, bool hint = false) : a(a), b(b), color(color), opacity(opacity), hint(hint) {}
};

/// Parallelogram graphic element
// FIXME: Implement as polygon
struct Parallelogram {
 vec2 min,max;
 float dy;
 bgr3f color = 0; float opacity = 1;
 Parallelogram(vec2 min, vec2 max, float dy, bgr3f color=0, float opacity=1) : min(min), max(max), dy(dy), color(color), opacity(opacity) {}
};

struct Cubic {
 buffer<vec2> points;
 bgr3f color = 0; float opacity = 1;
 Cubic(buffer<vec2>&& points, bgr3f color=0, float opacity=1) : points(move(points)), color(color), opacity(opacity) {}
};

/// Set of graphic elements
struct Graphics : shareable {
 vec2 offset = 0;
 Rect bounds = Rect(inff, -inff); // bounding box of untransformed primitives
 array<Fill> fills;
 array<Blit> blits;
 array<Glyph> glyphs;
 array<Line> lines;
 array<Parallelogram> parallelograms;
 array<Cubic> cubics;

 map<vec2, shared<Graphics>> graphics;

 Graphics(){}
 default_move(Graphics);
 virtual ~Graphics() {}

 void translate(vec2 offset) {
  bounds = offset+bounds;
  for(auto& o: fills) o.origin += offset;
  for(auto& o: blits) o.origin += offset;
  for(auto& o: glyphs) o.origin += offset;
  for(auto& o: parallelograms) { o.min+=offset; o.max+=offset; }
  for(auto& o: lines) { o.p0+=offset; o.p1+=offset; }
  for(auto& o: cubics) for(vec2& p: o.points) p+=vec2(offset);
 }
 void append(const Graphics& o) {
  assert_(!o.offset);
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
