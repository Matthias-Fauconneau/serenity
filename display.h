#pragma once
#include "image.h"
#include "vector.h"

void openDisplay();
extern int2 screen;

/// Rectangle
struct Rect {
    int2 min,max;
    explicit Rect(int2 max):min(0,0),max(max){}
    Rect(int2 min, int2 max):min(min),max(max){}
    /// Intersect this clip with \a clip
    Rect clip(Rect clip) { return Rect(::max(min,clip.min),::min(max,clip.max)); }
    /// Tests if this clip is empty
    explicit operator bool() { return (max-min)>int2(0,0); }
};
inline Rect operator +(int2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }
void push(Rect clip);
void pop();
void finish();

/// Fill framebuffer area in \a rect with \a color
void fill(Rect rect, rgb color);

/// Blit \a source to framebuffer at \a target
void blit(int2 target, const Image<gray>& source);
//void blit(int2 target, const Pixmap& source);
