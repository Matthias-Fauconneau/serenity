#include "graphics.h"

static uint8 sRGB_forward[0x1000];  // 4K (FIXME: interpolation of a smaller table might be faster)
void __attribute((constructor(1001))) generate_sRGB_forward() {
    for(uint index: range(sizeof(sRGB_forward))) {
        real linear = (real) index / (sizeof(sRGB_forward)-1);
        real sRGB = linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear;
        assert(abs(linear-(sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92))<exp2(-50));
        sRGB_forward[index] = round(0xFF*sRGB);
    }
}

float sRGB_reverse[0x100];
void __attribute((constructor(1001))) generate_sRGB_reverse() {
    for(uint index: range(0x100)) {
        real sRGB = (real) index / 0xFF;
        real linear = sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92;
        assert(abs(sRGB-(linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear))<exp2(-50));
        sRGB_reverse[index] = linear;
        assert(sRGB_forward[int(round(0xFFF*sRGB_reverse[index]))]==index);
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

void blend(const Image& target, uint x, uint y, vec3 color, float alpha) notrace;
void blend(const Image& target, uint x, uint y, vec3 source_linear, float alpha) {
    byte4& target_sRGB = target(x,y);
    vec3 target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
    int3 linearBlend = int3(round((0xFFF*(1-alpha))*vec3(target_linear) + (0xFFF*alpha)*source_linear));
    target_sRGB = byte4(sRGB_forward[linearBlend[0]], sRGB_forward[linearBlend[1]], sRGB_forward[linearBlend[2]],
            min(0xFF,target_sRGB.a+int(round(0xFF*alpha)))); // Additive alpha accumulation
}

void fill(const Image& target, Rect rect, vec3 color, float alpha) {
    rect = rect & Rect(target.size());
    color = clip(vec3(0), color, vec3(1));
    if(alpha<1) {
        for(int y: range(rect.min.y,rect.max.y)) for(int x: range(rect.min.x,rect.max.x)) {
            blend(target, x, y, color, alpha);
        }
    } else { // Solid fill
        int3 linear = int3(round(float(0xFFF)*color));
        byte4 sRGB = byte4(sRGB_forward[linear[0]], sRGB_forward[linear[1]], sRGB_forward[linear[2]], 0xFF);
        for(int y: range(rect.min.y,rect.max.y)) for(int x: range(rect.min.x,rect.max.x)) {
            target(x,y) = sRGB;
        }
    }
}

void blit(const Image& target, int2 position, const Image& source, vec3 color, float alpha) {
    Rect rect = (position+Rect(source.size())) & Rect(target.size());
    color = clip(vec3(0), color, vec3(1));
    if(color!=vec3(0) || alpha<1 || source.sRGB) {
        for(int y: range(rect.min.y,rect.max.y)) for(int x: range(rect.min.x,rect.max.x)) {
            byte4 RGBA = source(x-position.x,y-position.y);
            vec3 linear = source.sRGB ? vec3(sRGB_reverse[RGBA[0]], sRGB_reverse[RGBA[1]], sRGB_reverse[RGBA[2]]) : vec3(RGBA.bgr())/float(0xFF);
            blend(target, x, y, color*linear, alpha*RGBA.a/0xFF);
        }
    } else { // Alpha multiply (e.g. glyphs)
        for(int y: range(rect.min.y,rect.max.y)) for(int x: range(rect.min.x,rect.max.x)) {
            int alpha = source(x-position.x,y-position.y).a; // FIXME: single channel images
            byte4& target_sRGB = target(x,y);
            vec3 target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
            int3 linearBlend = int3(round((0xFFF*(1-float(alpha)/0xFF))*vec3(target_linear)));
            target_sRGB = byte4(sRGB_forward[linearBlend[0]], sRGB_forward[linearBlend[1]], sRGB_forward[linearBlend[2]],
                    min(0xFF,int(target_sRGB.a)+alpha)); // Additive alpha accumulation
        }
    }
}

void blend(const Image& target, uint x, uint y, vec3 color, float alpha, bool transpose) {
    if(transpose) swap(x,y);
    if(x>=target.width || y>=target.height) return;
    blend(target, x,y, color, alpha);
}

void line(const Image& target, float x1, float y1, float x2, float y2, vec3 color, float alpha) {
    color = clip(vec3(0), color, vec3(1));
    float dx = x2 - x1, dy = y2 - y1;
    bool transpose=false;
    if(abs(dx) < abs(dy)) { swap(x1, y1); swap(x2, y2); swap(dx, dy); transpose=true; }
    if(x2 < x1) { swap(x1, x2); swap(y1, y2); }
    if(dx==0) return; //p1==p2
    float gradient = dy / dx;
    int i1,i2; float intery;
    {
        float xend = round(x1), yend = y1 + gradient * (xend - x1);
        float xgap = 1 - fract(x1 + 1./2);
        blend(target, xend, yend, color, (1-fract(yend)) * xgap * alpha, transpose);
        blend(target, xend, yend+1, color, fract(yend) * xgap * alpha, transpose);
        i1 = int(xend);
        intery = yend + gradient;
    }
    {
        float xend = round(x2), yend = y2 + gradient * (xend - x2);
        float xgap = fract(x2 + 1./2);
        blend(target, xend, yend, color, (1-fract(yend)) * xgap * alpha, transpose);
        blend(target, xend, yend+1, color, fract(yend) * xgap * alpha, transpose);
        i2 = int(xend);
    }
    for(int x=i1+1;x<i2;x++) {
        blend(target, x, intery, color, (1-fract(intery)) * alpha, transpose);
        blend(target, x, intery+1, color, fract(intery) * alpha, transpose);
        intery += gradient;
    }
}
