#include "display.h"
#include "image.h"
#include "math.h"
#if GL
#include "gl.h"
FILE(display)
#endif

typedef vec<bgra,int,4> int4;

//FIXME: thread_local
//Lock framebufferLock;
bool softwareRendering = true;
int resolution = 96;
Image framebuffer;
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
        if(color==white) {
            for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                byte4 s = source(x-target.x,y-target.y); int a=s.a;
                byte4& d = framebuffer(x,y);
                byte4 t = byte4((int4(d)*(0xFF-a) + int4(s)*a)/0xFF); t.a=0xFF;
                d = t;
            }
        } else {
            for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                byte4 image_sRGB = source(x-target.x,y-target.y);
                float alpha = image_sRGB.a*color[3]/0xFF;
                byte4& target_sRGB = framebuffer(x,y);
                extern uint8 sRGB_lookup[256], inverse_sRGB_lookup[256];
                int3 image_linear (inverse_sRGB_lookup[image_sRGB[0]], inverse_sRGB_lookup[image_sRGB[1]], inverse_sRGB_lookup[image_sRGB[2]]);
                int3 target_linear(inverse_sRGB_lookup[target_sRGB[0]], inverse_sRGB_lookup[target_sRGB[1]], inverse_sRGB_lookup[target_sRGB[2]]);
                vec3 source_linear = alpha*color.xyz()*vec3(image_linear);
                int3 linearBlend = min(int3(0xFF), additiveBlend ?
                                           target_linear + int3(source_linear)
                                         : int3(round((1-alpha)*vec3(target_linear) + source_linear)) );
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

void bilinear(Image& target, const Image& source);
void blit(int2 target, const Image& source, int2 size) {
    Image region = clip(framebuffer, target, size);
    bilinear(region, source);
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
    x1 -= 1./2, x2 -= 1./2, y1 -= 1./2, y2 -= 1./2; // Pixel centers at 1/2
    float dx = x2 - x1, dy = y2 - y1;
    bool transpose=false;
    if(abs(dx) < abs(dy)) { swap(x1, y1); swap(x2, y2); swap(dx, dy); transpose=true; }
    if(x2 < x1) { swap(x1, x2); swap(y1, y2); }
    if(dx==0) return; //p1==p2
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

vec3 HSVtoRGB(float h, float s, float v) {
    float H = h*6, C = v*s, X = C*(1-abs(mod(H,2)-1));
    int i=H;
    if(i==0) return vec3(C,X,0);
    if(i==1) return vec3(X,C,0);
    if(i==2) return vec3(0,C,X);
    if(i==3) return vec3(0,X,C);
    if(i==4) return vec3(X,0,C);
    if(i==5) return vec3(C,0,X);
    return vec3(0,0,0);
}
