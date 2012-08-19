#pragma once
#include "vector.h"
#include "image.h"

/// Clip
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
void blit(int2 target, const Image<uint8>& source, uint8 opacity=255, bool invert=false);
/// Blits \a source to framebuffer at \a target with alpha blending if \a source.alpha is set
/// \note if \a invert is set, source is blitted with inverted and swapped colors (1-r,1-g,1-b,a), used for text selections with subpixel font
void blit(int2 target, const Image<byte4>& source, bool invert=false);
