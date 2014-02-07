#include "display.h"
#include "image.h"
#include "math.h"

typedef vec<bgra,int,4> int4;

Image framebuffer;
bool additiveBlend = false; // Defaults to alpha blend
array<Rect> clipStack;
Rect currentClip=Rect(0);

void fill(Rect rect, vec4 color) {
    rect = rect & currentClip;
    int4 color8 (color[0]*color.w*0xFF,color[1]*color.w*0xFF,color[2]*color.w*0xFF,color.w*0xFF); // Premultiply source alpha
    if(color8.a == 0xFF) {
        for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) framebuffer(x,y) = byte4(color8);
    } else {
        int a=color8.a;
        for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4& d = framebuffer(x,y);
            int4 t = additiveBlend ? min(int4(0xFF), int4(d) + color8) : int4(d)*(0xFF-a)/0xFF + color8;
            d = byte4(t.b, t.g, t.r, 0xFF);
        }
    }
}

void blit(int2 target, const Image& source, vec4 color) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    if(source.alpha) {
        if(color==white) {
            for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                byte4 s = source(x-target.x,y-target.y); int a=s.a;
                byte4& d = framebuffer(x,y);
                byte4 t = byte4((int4(d)*(0xFF-a) + int4(s)*a)/0xFF); t.a=0xFF;
                d = t;
            }
        } else {
            color = clip(vec4(0), color, vec4(1));
            for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                byte4 image_sRGB = source(x-target.x,y-target.y);
                float alpha = image_sRGB.a*color[3]/0xFF;
                byte4& target_sRGB = framebuffer(x,y);
                extern uint8 sRGB_lookup[256], inverse_sRGB_lookup[256];
                int3 image_linear = source.sRGB ?
                        int3(inverse_sRGB_lookup[image_sRGB[0]], inverse_sRGB_lookup[image_sRGB[1]], inverse_sRGB_lookup[image_sRGB[2]]) :
                        int3(image_sRGB.bgr());
                int3 target_linear(inverse_sRGB_lookup[target_sRGB[0]], inverse_sRGB_lookup[target_sRGB[1]], inverse_sRGB_lookup[target_sRGB[2]]);
                vec3 source_linear = alpha*color.xyz()*vec3(image_linear);
                int3 linearBlend = additiveBlend ? min(int3(0xFF), target_linear + int3(source_linear))
                                                 : int3(round((1-alpha)*vec3(target_linear) + source_linear));
                target_sRGB = byte4(sRGB_lookup[linearBlend[0]], sRGB_lookup[linearBlend[1]], sRGB_lookup[linearBlend[2]], 0xFF);
            }
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 t = source(x-target.x,y-target.y); t.a = 0xFF;
            framebuffer(x,y) = t;
        }
    }
}

void blend(int x, int y, vec4 color, float alpha) {
    byte4& target_sRGB = framebuffer(x,y);
    extern uint8 sRGB_lookup[256], inverse_sRGB_lookup[256];
    int3 target_linear(inverse_sRGB_lookup[target_sRGB[0]], inverse_sRGB_lookup[target_sRGB[1]], inverse_sRGB_lookup[target_sRGB[2]]);
    vec3 source_linear = alpha*color.xyz()*vec3(0xFF);
    int3 linearBlend = min(int3(0xFF), additiveBlend ?
                               target_linear + int3(source_linear)
                             : int3(round((1-alpha)*vec3(target_linear) + source_linear)) );
    target_sRGB = byte4(sRGB_lookup[linearBlend[0]], sRGB_lookup[linearBlend[1]], sRGB_lookup[linearBlend[2]], 0xFF);
}

inline void blend(int x, int y, vec4 color, float alpha, bool transpose) {
    if(transpose) swap(x,y);
    if(x<currentClip.min.x || x>=currentClip.max.x || y<currentClip.min.y || y>=currentClip.max.y) return;
    blend(x,y,color, alpha);
}
inline float fpart(float x) { return x-int(x); }
void line(float x1, float y1, float x2, float y2, vec4 color) {
    x1 -= 1./2, x2 -= 1./2, y1 -= 1./2, y2 -= 1./2; // Pixel centers at 1/2
    float dx = x2 - x1, dy = y2 - y1;
    bool transpose=false;
    if(abs(dx) < abs(dy)) { swap(x1, y1); swap(x2, y2); swap(dx, dy); transpose=true; }
    if(x2 < x1) { swap(x1, x2); swap(y1, y2); }
    if(dx==0) return; //p1==p2
    color = clip(vec4(0), color, vec4(1));
    float gradient = dy / dx;
    int i1,i2; float intery;
    {
        float xend = round(x1), yend = y1 + gradient * (xend - x1);
        float xgap = 1-fpart(x1 + 0.5);
        blend(xend, yend, color, (1-fpart(yend)) * xgap * color.w, transpose);
        blend(xend, yend+1, color, fpart(yend) * xgap * color.w, transpose);
        i1 = int(xend);
        intery = yend + gradient;
    }
    {
        float xend = round(x2), yend = y2 + gradient * (xend - x2);
        float xgap = fpart(x2 + 0.5);
        blend(xend, yend, color, (1-fpart(yend)) * xgap * color.w, transpose);
        blend(xend, yend+1, color, fpart(yend) * xgap * color.w, transpose);
        i2 = int(xend);
    }
    for(int x=i1+1;x<i2;x++) {
        blend(x, intery, color, (1-fpart(intery)) * color.w, transpose);
        blend(x, intery+1, color, fpart(intery) * color.w, transpose);
        intery += gradient;
    }
}

vec3 LChuvtoLuv(float L, float C, float h) {
    return vec3(L, C*cos(h) , C*sin(h));
}
vec3 LuvtoXYZ(float L, float u, float v) {
    const float xn=0.3127, yn=0.3290; // D65 white point (2° observer)
    const float un = 4*xn/(-2*xn+12*yn+3), vn = 9*yn/(-2*xn+12*yn+3);
    float u2 = un + u / (13*L);
    float v2 = vn + v / (13*L);
    float Y = L<=8 ? L * cb(3./29) : cb((L+16)/116);
    float X = Y * (9*u2)/(4*v2);
    float Z = Y * (12-3*u2-20*v2)/(4*v2);
    return vec3(X, Y, Z);
}
vec3 LuvtoXYZ(vec3 Luv) { return LuvtoXYZ(Luv[0], Luv[1], Luv[2]); }
vec3 XYZtoBGR(float X, float Y, float Z) {
    float R = + 3.240479 * X - 1.53715 * Y - 0.498535 * Z;
    float G = - 0.969256 * X + 1.875992 * Y + 0.041556 * Z;
    float B	= + 0.055648 * X - 0.204043 * Y + 1.057311 * Z;
    return vec3(B, G, R);
}
vec3 XYZtoBGR(vec3 XYZ) { return XYZtoBGR(XYZ[0], XYZ[1], XYZ[2]); }
vec3 LChuvtoBGR(float L, float C, float h) { return XYZtoBGR(LuvtoXYZ(LChuvtoLuv(L, C, h))); }
