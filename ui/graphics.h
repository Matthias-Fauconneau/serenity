#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "image.h"

// Colors
constexpr vec3 black(0, 0, 0);
constexpr vec3 darkGray(12./16, 12./16, 12./16);
constexpr vec3 lightGray (15./16, 15./16, 15./16);
constexpr vec3 white (1, 1, 1);
constexpr vec3 highlight (14./16, 12./16, 8./16);
constexpr vec3 blue (1, 0, 0);
constexpr vec3 green (0, 1, 0);
constexpr vec3 red (0, 0, 1);

/// Converts lightness, chroma, hue to linear sRGB
/// sRGB primaries:
/// Red: L~53.23, C~179.02, h~0.21
/// Green: L~87.74, C~135.80, h~2.23
/// Blue: L~32.28, C~130.61, h~-1.64
vec3 LChuvtoBGR(float L, float C, float h);

// Primitives

/// Blends pixel at \a x, \a y with \a color
void blend(const Image& target, uint x, uint y, vec3 color, float alpha);

/// Fills pixels inside \a rect with \a color
void fill(const Image& target, Rect rect, vec3 color=black, float alpha=1);

/// Blits \a source at \a target (with per pixel opacity if \a source.alpha is set)
void blit(const Image& target, int2 position, const Image& source, vec3 color=white, float alpha=1);

/// Draws a thin antialiased line from p1 to p2
void line(const Image& target, float x1, float y1, float x2, float y2, vec3 color=black, float alpha=1);
inline void line(const Image& target, vec2 p1, vec2 p2, vec3 color=black, float alpha=1) { line(target, p1.x, p1.y, p2.x, p2.y, color, alpha); }
inline void line(const Image& target, int2 p1, int2 p2, vec3 color=black, float alpha=1) { line(target, p1.x,p1.y,p2.x,p2.y, color, alpha); }

/// Draws a parallelogram parallel to the Y axis
void parallelogram(const Image& target, int2 p0, int2 p1, int dy, vec3 color=black, float alpha=1);
