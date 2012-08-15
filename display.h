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
inline string str(const Rect& r) { return "Rect("_+str(r.min)+" - "_+str(r.max)+")"_; }

/// Remove any intersections between \a neg and \a rects, subdividing as necessary
/// \note Used to fill background accurately to optimize bandwidth usage (and avoid flickering if rendering to front)
void remove(array<Rect>& rects, Rect neg);

extern array<Rect> clipStack;
extern Rect currentClip;
inline void push(Rect clip) { clipStack << currentClip; currentClip=currentClip & clip; }
inline void pop() { currentClip=clipStack.pop(); }

/// Linux framebuffer (mapped by \a display() and not lazily to avoid extern calls in loops)
extern Image<pixel> framebuffer;

constexpr byte4 black i({0, 0, 0, 255});
constexpr byte4 gray i({128, 128, 128, 255});
constexpr byte4 lightGray i({192, 192, 192, 255});
constexpr byte4 white i({255, 255, 255, 255});
constexpr byte4 backgroundColor i({255, 255, 255, 255});
constexpr byte4 selectionColor i({224, 192, 128, 255});

/// Fills framebuffer pixels inside \a rect with \a color
void fill(Rect rect, byte4 color=backgroundColor);
/// Blits single channel intensity/alpha \a source to framebuffer at \a target
/// \note if \a invert is set, source is blitted with inverted intensity (1-v,1-v,1-v,v), useful for text selections
void blit(int2 target, const Image<uint8>& source, bool invert=false);
/// Blits \a source to framebuffer at \a target with alpha blending if \a source.alpha is set
/// \note if \a invert is set, source is blitted with inverted and swapped colors (1-r,1-g,1-b,a), used for text selections with subpixel font
void blit(int2 target, const Image<byte4>& source, bool invert=false);
