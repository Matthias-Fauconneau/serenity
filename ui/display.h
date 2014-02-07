#pragma once
/// \file display.h 2D graphics primitives (fill, blit, line)
#include "vector.h"
#include "rect.h"
#include "image.h"
#include "thread.h"

//FIXME: thread_local
/// Framebuffer of the window being rendered (X11 shared memory to be used only under Window::render)
extern Image framebuffer;
extern bool additiveBlend;
extern array<Rect> clipStack;
extern Rect currentClip;
inline void push(Rect clip) { clipStack << currentClip; currentClip=currentClip & clip; }
inline void pop() { currentClip=clipStack.pop(); }

// Colors
constexpr vec4 black(0, 0, 0, 1);
constexpr vec4 darkGray(12./16, 12./16, 12./16, 1);
constexpr vec4 lightGray (15./16, 15./16, 15./16, 1);
constexpr vec4 white (1, 1, 1, 1);
constexpr vec4 highlight (14./16, 12./16, 8./16, 1);
constexpr vec4 blue (1, 0, 0, 1);
constexpr vec4 cyan (1, 1, 0, 1);
constexpr vec4 green (0, 1, 0, 1);
constexpr vec4 yellow (0, 1, 1, 1);
constexpr vec4 red (0, 0, 1, 1);
constexpr vec4 magenta (0, 1, 1, 1);

// Primitives
/// Fills pixels inside \a rect with \a color
void fill(Rect rect, vec4 color=black);
inline void fill(int x1, int y1, int x2, int y2, vec4 color=black) { fill(Rect(int2(x1,y1),int2(x2,y2)),color); }

/// Blits \a source at \a target (with per pixel opacity if \a source.alpha is set)
void blit(int2 target, const Image& source, vec4 color=white);

/// Blends linear \a color to sRGB pixel at \a x,y
void blend(int x, int y, vec4 color, float alpha=1);

/// Draws a thin antialiased line from p1 to p2
void line(float x1, float y1, float x2, float y2, vec4 color=black);
inline void line(vec2 p1, vec2 p2, vec4 color=black) { line(p1.x,p1.y,p2.x,p2.y,color); }
inline void line(int2 p1, int2 p2, vec4 color=black) { line(p1.x+1./2,p1.y+1./2,p2.x+1./2,p2.y+1./2,color); }

/// Converts lightness, chroma, hue to linear sRGB
/// sRGB primaries:
/// Red: L~53.23, C~179.02, h~0.21
/// Green: L~87.74, C~135.80, h~2.23
/// Blue: L~32.28, C~130.61, h~-1.64
vec3 LChuvtoBGR(float L, float C, float h);
