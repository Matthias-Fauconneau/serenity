#include "display.h"
#include "math.h"
#if GL
#include "gl.h"
FILE(display)
#endif

typedef vector<bgra,int,4> int4;

bool softwareRendering = true;
int resolution = 96;
Image framebuffer;
Lock framebufferLock;
array<Rect> clipStack;
Rect currentClip=Rect(0);
bool additiveBlend = false; // Defaults to alpha blend

#if GL
int2 viewportSize;
vec2 vertex(float x, float y) { return vec2(2.*x/viewportSize.x-1,1-2.*y/viewportSize.y); }
vec2 vertex(vec2 v) { return vertex(v.x, v.y); }
Vertex vertex(Rect r, float x, float y) {
    return {vertex(x,y), vec2(int2(x,y)-r.min)/vec2(r.size())};
}
GLShader& fillShader() { static GLShader shader(display()); return shader; }
GLShader& blitShader() { static GLShader shader(display(), {"blit"_}); return shader; }
#endif

void fill(Rect rect, vec4 color) {
    rect = rect & currentClip;
#if GL
    if(!softwareRendering) {
        //if(color.w!=1) glBlendAlpha(); else glBlendNone();
        glBlendSubstract();
        GLShader& fill = fillShader();
        //fill["color"_] = color;
        //fill["color"_] = vec4(1)-color; //color;
        fill["color"] = vec4(vec3(1)-color.xyz(),1.f);
        GLVertexBuffer vertexBuffer;
        vertexBuffer.upload<vec2>({vertex(rect.min.x,rect.min.y),vertex(rect.max.x,rect.min.y),
                                   vertex(rect.min.x,rect.max.y),vertex(rect.max.x,rect.max.y)});
        vertexBuffer.bindAttribute(fill, "position"_, 2);
        vertexBuffer.draw(TriangleStrip);
        return;
    }
#endif
    int4 color8 (color.z*color.w*0xFF,color.y*color.w*0xFF,color.x*color.w*0xFF,color.w*0xFF); // Premultiply source alpha
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
#if GL
    if(!softwareRendering) {
        glBlendSubstract(); //if(source.alpha) glBlendSubstract(); /*FIXME*/ else glBlendNone();
        GLShader& blit = blitShader();
        blit["color"_] = vec4(1)-color;
        GLTexture texture (source/*, SRGB*/); //FIXME
        blit["sampler"_]=0; texture.bind(0);
        GLVertexBuffer vertexBuffer;
        Rect texRect = target+Rect(source.size());
        vertexBuffer.upload<Vertex>({vertex(texRect, rect.min.x,rect.min.y),vertex(texRect, rect.max.x,rect.min.y),
                                     vertex(texRect, rect.min.x,rect.max.y),vertex(texRect, rect.max.x,rect.max.y)});
        vertexBuffer.bindAttribute(blit, "position"_, 2, offsetof(Vertex, position));
        vertexBuffer.bindAttribute(blit, "texCoord"_, 2, offsetof(Vertex, texCoord));
        vertexBuffer.draw(TriangleStrip);
        return;
    }
#endif
    if(source.alpha) {
        int4 color8 = int4(color.z*0xFF,color.y*0xFF,color.x*0xFF,color.w*0xFF);
        if(color==white) {
            for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                byte4 s = source(x-target.x,y-target.y); int a=s.a;
                byte4& d = framebuffer(x,y);
                byte4 t = byte4((int4(d)*(0xFF-a) + int4(s)*a)/0xFF); t.a=0xFF;
                d = t;
            }
        } else {
            for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                byte4 s = source(x-target.x,y-target.y); int a=color8.a*int(s.a)/0xFF;
                byte4& d = framebuffer(x,y);
                byte4 t = byte4((int4(d)*(0xFF-a) + color8*int4(s)/0xFF*a)/0xFF); t.a=0xFF;
                d = t;
            }
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 t = source(x-target.x,y-target.y); t.a = 0xFF;
            framebuffer(x,y) = t;
        }
    }
}

void bilinear(Image& target, const Image& source);
void blit(int2 target, const Image& source, int2 size) {
    Image region = clip(framebuffer, target, size);
    bilinear(region, source);
}

inline void plot(int x, int y, float alpha, bool transpose, vec4 color) {
    if(transpose) swap(x,y);
    if(x>=currentClip.min.x && x<currentClip.max.x && y>=currentClip.min.y && y<currentClip.max.y) {
        byte4& sRGB = framebuffer(x,y);
        extern uint8 sRGB_lookup[256], inverse_sRGB_lookup[256];
        byte4 linear (inverse_sRGB_lookup[sRGB.b], inverse_sRGB_lookup[sRGB.g], inverse_sRGB_lookup[sRGB.r], 0);
        int4 linearBlend = min(int4(0xFF), additiveBlend ? int4(linear) + int4(alpha*color) : int4(round((1-alpha)*vec4(linear) + alpha*color))); // Blend
        sRGB = byte4(sRGB_lookup[linearBlend.b], sRGB_lookup[linearBlend.g], sRGB_lookup[linearBlend.r], 0xFF);
    }
}

inline float fpart(float x) { return x-int(x); }
inline float rfpart(float x) { return 1 - fpart(x); }
void line(float x1, float y1, float x2, float y2, vec4 color) {
#if GL
    if(!softwareRendering) {
        glBlendSubstract();
        GLShader& fill = fillShader();
        fill["color"] = vec4(vec3(1)-color.xyz(),1.f);
        //fill["color"_] = //vec4(color.xyz(),1.f);
        //glDrawLine(fill, p1, p2);
        GLVertexBuffer vertexBuffer;
        vertexBuffer.upload<vec2>({vertex(x1, y1),vertex(x2, y2)});
        vertexBuffer.bindAttribute(fill, "position"_, 2);
        vertexBuffer.draw(Lines);
        return;
    }
#endif
    vec4 color8 (color.z*0xFF,color.y*0xFF,color.x*0xFF,0xFF);
    float dx = x2 - x1, dy = y2 - y1;
    bool transpose=false;
    if(abs(dx) < abs(dy)) swap(x1, y1), swap(x2, y2), swap(dx, dy), transpose=true;
    if(x2 < x1) swap(x1, x2), swap(y1, y2);
    float gradient = dy / dx;
    int i1,i2; float intery;
    {
        float xend = round(x1), yend = y1 + gradient * (xend - x1);
        float xgap = rfpart(x1 + 0.5);
        plot(int(xend), int(yend), rfpart(yend) * xgap, transpose, color8);
        plot(int(xend), int(yend)+1, fpart(yend) * xgap, transpose, color8);
        i1 = int(xend);
        intery = yend + gradient;
    }
    {
        float xend = round(x2), yend = y2 + gradient * (xend - x2);
        float xgap = fpart(x2 + 0.5);
        plot(int(xend), int(yend), rfpart(yend) * xgap, transpose, color8);
        plot(int(xend), int(yend) + 1, fpart(yend) * xgap, transpose, color8);
        i2 = int(xend);
    }
    for(int x=i1+1;x<i2;x++) {
        plot(x, int(intery), rfpart(intery), transpose, color8);
        plot(x, int(intery)+1, fpart(intery), transpose, color8);
        intery += gradient;
    }
}
