#pragma once
#include "vector.h"

static vec3 LChuvtoLuv(float L, float C, float h) {
 return vec3(L, C*cos(h) , C*sin(h));
}
static vec3 LuvtoXYZ(float L, float u, float v) {
	const float xn=0.3127, yn=0.3290; // D65 white point (2° observer)
	const float un = 4*xn/(-2*xn+12*yn+3), vn = 9*yn/(-2*xn+12*yn+3);
	float u2 = un + u / (13*L);
	float v2 = vn + v / (13*L);
 float Y = L</*=*/8 ? L * cb(3./29) : cb((L+16)/116);
	float X = Y * (9*u2)/(4*v2);
	float Z = Y * (12-3*u2-20*v2)/(4*v2);
 return vec3(X, Y, Z);
}
static vec3 LuvtoXYZ(vec3 Luv) { return LuvtoXYZ(Luv[0], Luv[1], Luv[2]); }
static bgr3f XYZtoBGR(float X, float Y, float Z) { // sRGB
    float R = + 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
    float G = - 0.9689 * X + 1.8758 * Y + 0.0415 * Z;
    float B	= + 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
	return bgr3f(B, G, R);
}
static bgr3f XYZtoBGR(vec3 XYZ) { return XYZtoBGR(XYZ[0], XYZ[1], XYZ[2]); }
/// Converts lightness, chroma, hue to linear sRGB
/// sRGB primaries:
/// Red: L~53.23, C~179.02, h~0.21
/// Green: L~87.74, C~135.80, h~2.23
/// Blue: L~32.28, C~130.61, h~-1.64
bgr3f LChuvtoBGR(float L, float C, float h) { return XYZtoBGR(LuvtoXYZ(LChuvtoLuv(L, C, h))); }
