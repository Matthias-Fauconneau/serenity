#pragma once
#include "vector.h"
#include "image.h"

/// Clip
extern array<Rect> clipStack;
extern Rect currentClip;
inline void push(Rect clip) { clipStack << currentClip; currentClip=currentClip & clip; }
inline void pop() { currentClip=clipStack.pop(); }

/// Current window framebuffer (X11 shared memory mapped by Window::render)
extern Image framebuffer;

constexpr byte4 black __(0, 0, 0, 0xFF);
constexpr byte4 darken __(0x00, 0x00, 0x00, 0x40);
constexpr byte4 lighten __(0xE0, 0xE0, 0xE0, 0xFF);
constexpr byte4 highlight __(0xE0, 0xC0, 0x80, 0xFF);
constexpr byte4 white __(0xFF, 0xFF, 0xFF, 0xFF);

/// Fills pixels inside \a rect with \a color
void fill(Rect rect, byte4 color=black);
/// Blits \a source at \a target (with per pixel opacity if \a source.alpha is set)
/// \arg opacity multiplies alpha channel by opacity/255, alpha is accumulated in framebuffer
void blit(int2 target, const Image& source, uint8 opacity=255);
/// Multiplies pixels at \a target with \a source
/// \note \a source alpha * opacity/255 is accumulated in framebuffer
void multiply(int2 target, const Image& source, uint8 opacity=255);
