#include "graphics.h"

static void blend(const Image& target, uint x, uint y, vec3 source_linear, float alpha) {
    byte4& target_sRGB = target(x,y);
    vec3 target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
    int3 linearBlend = int3(round((0xFFF*(1-alpha))*vec3(target_linear) + (0xFFF*alpha)*source_linear));
    target_sRGB = byte4(sRGB_forward[linearBlend[0]], sRGB_forward[linearBlend[1]], sRGB_forward[linearBlend[2]],
            min(0xFF,target_sRGB.a+int(round(0xFF*alpha)))); // Additive alpha accumulation
}

#if FILL
static void fill(uint* target, uint stride, uint w, uint h, uint value) {
    for(uint y=0; y<h; y++) {
        for(uint x=0; x<w; x++) target[x] = value;
        target += stride;
    }
}

void fill(const Image& target, Rect rect, vec3 color, float alpha) {
    rect = rect & Rect(target.size);
    if(rect) {
        color = clip(vec3(0), color, vec3(1));
        if(alpha<1) {
            for(int y: range(rect.min.y,rect.max.y)) for(int x: range(rect.min.x,rect.max.x)) {
                blend(target, x, y, color, alpha);
            }
        } else { // Solid fill
            int3 linear = int3(round(float(0xFFF)*color));
            byte4 sRGB = byte4(sRGB_forward[linear[0]], sRGB_forward[linear[1]], sRGB_forward[linear[2]], 0xFF);
            fill((uint*)target.data+rect.min.y*target.stride+rect.min.x, target.stride, rect.max.x-rect.min.x, rect.max.y-rect.min.y, (uint&)sRGB);
        }
    }
}
#endif

static void blit(const Image& target, int2 position, const Image& source, vec3 color, float alpha) {
    assert_(source);
    Rect rect = (position+Rect(source.size)) & Rect(target.size);
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

#if LINE
static void blend(const Image& target, uint x, uint y, vec3 color, float alpha, bool transpose) {
    if(transpose) swap(x,y);
    if(x>=target.width || y>=target.height) return;
    blend(target, x,y, color, alpha);
}

void line(const Image& target, vec2 p1, vec2 p2, vec3 color, float alpha) {
    color = clip(vec3(0), color, vec3(1));
    float dx = p2.x - p1.x, dy = p2.y - p1.y;
    bool transpose=false;
    if(abs(dx) < abs(dy)) { swap(p1.x, p1.y); swap(p2.x, p2.y); swap(dx, dy); transpose=true; }
    if(p1.x > p2.x) { swap(p1.x, p2.x); swap(p1.y, p2.y); }
    if(dx==0) return; //p1==p2
    float gradient = dy / dx;
    int i1,i2; float intery;
    {
        float xend = round(p1.x), yend = p1.y + gradient * (xend - p1.x);
        float xgap = 1 - fract(p1.x + 1./2);
        blend(target, xend, yend, color, (1-fract(yend)) * xgap * alpha, transpose);
        blend(target, xend, yend+1, color, fract(yend) * xgap * alpha, transpose);
        i1 = int(xend);
        intery = yend + gradient;
    }
    {
        float xend = round(p2.x), yend = p2.y + gradient * (xend - p2.x);
        float xgap = fract(p2.x + 1./2);
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
#endif

#if POLYGON
void parallelogram(const Image& target, int2 p0, int2 p1, int dy, vec3 color, float alpha) {
    if(p0.x > p1.x) swap(p0.x, p1.x);
    for(uint x: range(max(0,p0.x), min(int(target.width),p1.x))) {
        float y0 = float(p0.y) + float((p1.y - p0.y) * int(x - p0.x)) / float(p1.x - p0.x); // FIXME: step
        float f0 = floor(y0);
        float coverage = (y0-f0);
        int i0 = int(f0);
        if(uint(i0)<target.height) blend(target, x, i0, color, alpha*(1-coverage));
        for(uint y: range(max(0,i0+1), min(int(target.height),i0+1+dy-1))) { // FIXME: clip once
            blend(target, x,y, color, alpha); // FIXME: antialias
        }
        if(uint(i0)<target.height) blend(target, x, i0+dy, color, alpha*coverage);
    }
}

// 8bit signed integer (for edge flags)
struct Image8 {
    Image8(uint width, uint height) : width(width), height(height) {
        assert(width); assert(height);
        data = ::buffer<int8>(height*width);
        data.clear(0);
    }
    int8& operator()(uint x, uint y) {assert(x<width && y<height, x, y, width, height); return data[y*width+x]; }
    buffer<int8> data;
    uint width, height;
};

// FIXME: Coverage integration
static int lastStepY = 0; // Do not flag first/last point twice but cancel on direction changes
static void line(Image8& raster, int2 p0, int2 p1) {
    int x0=p0.x, y0=p0.y, x1=p1.x, y1=p1.y;
    int dx = abs(x1-x0);
    int dy = abs(y1-y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx-dy;
    if(sy!=lastStepY) raster(x0,y0) -= sy;
    for(;;) {
        if(x0 == x1 && y0 == y1) break;
        int e2 = 2*err;
        if(e2 > -dy) { err -= dy, x0 += sx; }
        if(e2 < dx) { err += dx, y0 += sy; raster(x0,y0) -= sy; } // Only rasters at y step
    }
    lastStepY=sy;
}
#endif

#if CUBIC
static vec2 cubic(vec2 A,vec2 B,vec2 C,vec2 D,float t) { return ((1-t)*(1-t)*(1-t))*A + (3*(1-t)*(1-t)*t)*B + (3*(1-t)*t*t)*C + (t*t*t)*D; }
static void cubic(Image8& raster, vec2 A, vec2 B, vec2 C, vec2 D) {
    const int N = 16; //FIXME
    int2 a = int2(round(A));
    for(int i : range(1,N+1)) {
        int2 b = int2(round(cubic(A,B,C,D,float(i)/N)));
        line(raster, a, b);
        a=b;
    }
}

// Renders cubic spline (two control points between each end point)
void cubic(const Image& target, const ref<vec2>& points, vec3 color, float alpha, const uint oversample) {
    vec2 pMin = vec2(target.size), pMax = 0;
    for(vec2 p: points) pMin = ::min(pMin, p), pMax = ::max(pMax, p);
    pMin = floor(pMin), pMax = ceil(pMax);
    const int2 iMin = int2(pMin), iMax = int2(pMax);
    const int2 cMin = max(int2(0),iMin), cMax = min(target.size, iMax);
    if(!(cMin < cMax)) return;
    const int2 size = iMax-iMin;
    Image8 raster(oversample*size.x+1,oversample*size.y+1);
    lastStepY = 0;
    for(uint i=0;i<points.size; i+=3) {
        cubic(raster, float(oversample)*(points[i]-pMin), float(oversample)*(points[(i+1)%points.size]-pMin), float(oversample)*(points[(i+2)%points.size]-pMin),
                float(oversample)*(points[(i+3)%points.size]-pMin));
    }
    for(uint y: range(cMin.y, cMax.y)) {
        int acc[oversample]; mref<int>(acc,oversample).clear(0);
        for(uint x: range(iMin.x, cMin.x)) for(uint j: range(oversample)) for(uint i: range(oversample)) acc[j] += raster((x-iMin.x)*oversample+i, (y-iMin.y)*oversample+j);
        for(uint x: range(cMin.x, cMax.x)) { //Supersampled rasterization
            int coverage = 0;
            for(uint j: range(oversample)) for(uint i: range(oversample)) {
                acc[j] += raster((x-iMin.x)*oversample+i, (y-iMin.y)*oversample+j);
                //assert_(acc[j]>=0, acc[j]);
                coverage += acc[j]!=0;
            }
            if(coverage) blend(target, x, y, color, float(coverage)/sq(oversample)*alpha);
        }
    }
}
#endif

void render(const Image& target, const Graphics& graphics) {
    for(const auto& e: graphics.blits) {
        assert(e.image.width && e.image.height, e.image.size);
        if(int2(e.size) == e.image.size) blit(target, int2(round(e.origin)), e.image, 1, 1);
        else blit(target, int2(round(e.origin)), resize(int2(e.size), e.image), 1, 1);
    }
    for(const auto& e: graphics.glyphs) {
        Font::Glyph glyph = e.font.render(e.font.index(e.code));
        blit(target, int2(round(e.origin))+glyph.offset, glyph.image, 0, 1);
    }
}