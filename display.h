#pragma once
#include "vector.h"
#include "image.h"

/// Clip
struct Rect {
    int2 min,max;
    explicit Rect(int2 max):min(0,0),max(max){}
    Rect(int2 min, int2 max):min(min),max(max){}
    bool contains(int2 p) { return p>=min && p<max; }
    explicit operator bool() { return (max-min)>int2(0,0); }
};
inline Rect operator +(int2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }
inline Rect operator |(Rect a, Rect b) { return Rect(min(a.min,b.min),max(a.max,b.max)); }
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min),min(a.max,b.max)); }
inline bool operator ==(Rect a, Rect b) { return a.min==b.min && a.max==b.max; }

extern array<Rect> clipStack;
extern Rect currentClip;
inline void push(Rect clip) { clipStack << currentClip; currentClip=currentClip & clip; }
inline void pop() { currentClip=clipStack.pop(); }

/// Current framebuffer (X11 shared memory mapped by Window::render)
extern Image<pixel> framebuffer;

constexpr byte4 black __(0, 0, 0, 0xFF);
constexpr byte4 gray __(0xC0, 0xC0, 0xC0, 0xFF);
constexpr byte4 lightGray __(0xE0, 0xE0, 0xE0, 0xFF);
constexpr byte4 white __(0xFF, 0xFF, 0xFF, 0xFF);
constexpr byte4 selectionColor __(0xE0, 0xC0, 0x80, 0xFF);

/// Fills framebuffer pixels inside \a rect with \a color
void fill(Rect rect, byte4 color);
/// Blits single channel intensity/alpha \a source to framebuffer at \a target
/// \note if \a invert is set, source is blitted with inverted intensity (1-v,1-v,1-v,v), useful for text selections
void blit(int2 target, const Image<uint8>& source, bool invert=false);
/// Blits \a source to framebuffer at \a target with alpha blending if \a source.alpha is set
/// \note if \a invert is set, source is blitted with inverted and swapped colors (1-r,1-g,1-b,a), used for text selections with subpixel font
void blit(int2 target, const Image<byte4>& source, bool invert=false);
