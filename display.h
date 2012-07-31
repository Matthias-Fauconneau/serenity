#pragma once
#include "vector.h"
#include "image.h"

/// Clip
struct Rect {
    int2 min,max;
    explicit Rect(int2 max):min{0,0},max(max){}
    Rect(int2 min, int2 max):min(min),max(max){}
    /// Intersect this clip with \a clip
    Rect clip(Rect clip) { return Rect(::max(min,clip.min),::min(max,clip.max)); }
    bool contains(int2 p) { return p>=min && p<max; }
    explicit operator bool() { return (max-min)>int2(0,0); }
};
inline Rect operator +(int2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }
inline Rect operator |(Rect a, Rect b) { return Rect(min(a.min,b.min),max(a.max,b.max)); }
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min),min(a.max,b.max)); }
inline string str(const Rect& r) { return "Rect("_+str(r.min)+" - "_+str(r.max)+")"_; }

void push(Rect clip);
void pop();
void finish();

/// Returns display size (and on first call mmap framebuffer)
int2 display();

/// Fill framebuffer area in \a rect with \a color
void fill(Rect rect, pixel color);

/// Blit \a source to framebuffer at \a target
void blit(int2 target, const Image<uint8>& source); //TODO: color
void blit(int2 target, const Image<pixel>& source);
void blit(int2 target, const Image<byte4>& source);

/// Fast software cursor
void patchCursor(int2 position, const Image<byte4>& cursor, bool repair=true);
