#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "image.h"
#include "font.h"

/// Axis-aligned rectangle with 2D integer coordinates
struct Rect {
    int2 min,max;
    Rect(int2 min, int2 max):min(min),max(max){}
    explicit Rect(int2 max):min(0,0),max(max){}
    explicit operator bool() { return min<max; }
    bool contains(int2 p) { return p>=min && p<max; }
    int2 position() { return min; }
    int2 size() { return max-min; }
};
inline bool operator ==(const Rect& a, const Rect& b) { return a.min==b.min && a.max==b.max; }
inline bool operator >(const Rect& a, const Rect& b) { return a.min<b.min || a.max>b.max; }
inline Rect operator +(int2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min),min(a.max,b.max)); }
inline Rect operator |(Rect a, Rect b) { return Rect(min(a.min,b.min),max(a.max,b.max)); }
inline String str(const Rect& r) { return "Rect("_+str(r.min)+" - "_+str(r.max)+")"_; }

/// Image graphic element
struct Blit {
    vec2 origin, size;
    Image image;
};

/// Text graphic element
struct Glyph {
    vec2 origin;
    Font& font;
    uint code;
};

/// Set of graphic elements
struct Graphics {
    array<Blit> blits;
    array<Glyph> glyphs;
    Graphics& append(const Graphics& o, vec2 offset) {
        for(const auto& e: o.blits) blits << Blit{offset+e.origin, e.size, share(e.image)};
        for(auto e: o.glyphs) { e.origin += offset; glyphs << e; }
        return *this;
    }
};
