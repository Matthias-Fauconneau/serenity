#pragma once
/// \file display.h Graphics primitives (fill, blit, substract)
#include "vector.h"
#include "image.h"

// Clip
extern array<Rect> clipStack;
extern Rect currentClip;
inline void push(Rect clip) { clipStack << currentClip; currentClip=currentClip & clip; }
inline void pop() { currentClip=clipStack.pop(); }

/// Current window framebuffer (X11 shared memory mapped by Window::render)
extern Image framebuffer;

constexpr byte4 black __(0, 0, 0, 0xFF);
constexpr byte4 darken __(0x00, 0x00, 0x00, 0x40);
constexpr byte4 lighten __(0xFF, 0xFF, 0xFF, 0x40);
constexpr byte4 highlight __(0xE0, 0xC0, 0x80, 0xFF);
constexpr byte4 white __(0xFF, 0xFF, 0xFF, 0xFF);
constexpr byte4 blue __(0xFF, 0, 0, 0xFF);
constexpr byte4 cyan __(0xFF, 0xFF, 0, 0xFF);
constexpr byte4 green __(0, 0xFF, 0, 0xFF);
constexpr byte4 yellow __(0, 0xFF, 0xFF, 0xFF);
constexpr byte4 red __(0, 0, 0xFF, 0xFF);
constexpr byte4 magenta __(0xFF, 0, 0xFF, 0xFF);

/// Fills pixels inside \a rect with \a color
void fill(Rect rect, byte4 color=black, bool blend=true);

/// Blits \a source at \a target (with per pixel opacity if \a source.alpha is set)
/// \a opacity multiplies alpha channel by opacity/255, alpha is accumulated in framebuffer
void blit(int2 target, const Image& source, uint8 opacity=255);
/// Substracts \a source from \a target
void substract(int2 target, const Image& source, byte4 color=black);

/// Draws a thin antialiased line from (x1, y1) to (x2,y2)
void line(float x1, float y1, float x2, float y2, byte4 color=black);

/// Draws a convex polygon
template<uint N> void polygon(vec2 polygon[N], byte4 color=black);
/// Draws a triangle
inline void triangle(vec2 A, vec2 B, vec2 C, byte4 color=black) { polygon<3>((vec2[]){A,B,C},color); }
/// Draws a convex quad
inline void quad(vec2 A, vec2 B, vec2 C, vec2 D, byte4 color=black) { polygon<4>((vec2[]){A,B,C,D},color); }

/// Draws a circle
void circle(vec2 A, float r, byte4 color=black);

/// Draws a thick line (trapezoid) from \a a to \a b with width interpolated from \a wa to \a wb
void line(vec2 a, vec2 b, float wa=1, float wb=1, byte4 color=black);
/// Draws a thick line (rectangle) from \a a to \a b
inline void line(vec2 a, vec2 b, float w=1, byte4 color=black) { line(a,b,w,w,color); }
