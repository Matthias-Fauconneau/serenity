#pragma once
#include "vector.h"
#include "image.h"

/// Clip
extern array<Rect> clipStack;
extern Rect currentClip;
inline void push(Rect clip) { clipStack << currentClip; currentClip=currentClip & clip; }
inline void pop() { currentClip=clipStack.pop(); }

/// Current framebuffer (X11 shared memory mapped by Window::render)
extern Image framebuffer;

constexpr byte4 black __(0, 0, 0, 0xFF);
constexpr byte4 gray __(0xC0, 0xC0, 0xC0, 0xFF);
constexpr byte4 lightGray __(0xE0, 0xE0, 0xE0, 0xFF);
constexpr byte4 white __(0xFF, 0xFF, 0xFF, 0xFF);
constexpr byte4 selectionColor __(0xE0, 0xC0, 0x80, 0xFF);

/// Fills framebuffer pixels inside \a rect with \a color
void fill(Rect rect, byte4 color);
/// Blits \a source to framebuffer at \a target with alpha blending if \a source.alpha is set
/// \arg opacity multiply alpha channel by opacity/255
void blit(int2 target, const Image& source, uint8 opacity=255);
