#pragma once
/// \file display.h 2D graphics primitives (fill, blit, line)
#include "vector.h"
#include "rect.h"
#include "image.h"
#include "thread.h"

extern bool softwareRendering;
/// Framebuffer of the window being rendered (X11 shared memory to be used only under Window::render)
extern Image framebuffer;
extern Lock framebufferLock;
extern int resolution; /// Device resolution in dots per inch (dpi)
extern bool additiveBlend;

#if GL
extern int2 viewportSize;
vec2 vertex(float x, float y);
vec2 vertex(vec2 v);
struct Vertex { vec2 position, texCoord; };
Vertex vertex(Rect r, float x, float y);
#endif

// Clip
extern array<Rect> clipStack;
extern Rect currentClip;
inline void push(Rect clip) { clipStack << currentClip; currentClip=currentClip & clip; }
inline void pop() { currentClip=clipStack.pop(); }

// Colors
constexpr vec4 black(0, 0, 0, 1);
constexpr vec4 darkGray(12./16, 12./16, 12./16, 1);
constexpr vec4 lightGray (15./16, 15./16, 15./16, 1);
constexpr vec4 white (1, 1, 1, 1);
constexpr vec4 highlight (8./16, 12./16, 14./16, 1);
constexpr vec4 blue (0, 0, 1, 1);
constexpr vec4 cyan (0, 1, 1, 1);
constexpr vec4 green (0, 1, 0, 1);
constexpr vec4 yellow (0, 1, 1, 1);
constexpr vec4 red (1, 0, 0, 1);
constexpr vec4 magenta (1, 0, 1, 1);

// Primitives
/// Fills pixels inside \a rect with \a color
void fill(Rect rect, vec4 color=black);
inline void fill(int x1, int y1, int x2, int y2, vec4 color=black) { fill(Rect(int2(x1,y1),int2(x2,y2)),color); }

/// Blits \a source at \a target (with per pixel opacity if \a source.alpha is set)
void blit(int2 target, const Image& source, vec4 color=white);

/// Resizes \a source to \a size and blits at \a target
void blit(int2 target, const Image& source, int2 size);

/// Draws a thin antialiased line from p1 to p2
void line(float x1, float y1, float x2, float y2, vec4 color=black);
inline void line(vec2 p1, vec2 p2, vec4 color=black) { line(p1.x,p1.y,p2.x,p2.y,color); }
inline void line(int2 p1, int2 p2, vec4 color=black) { line(p1.x,p1.y,p2.x,p2.y,color); }
