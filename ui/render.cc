#include "render.h"

extern uint8 sRGB_forward[0x1000];
extern float sRGB_reverse[0x100];

static void blend(const Image& target, uint x, uint y, bgr3f source_linear, float opacity) {
 byte4& target_sRGB = target(x,y);
 bgr3f target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
 bgr3i linearBlend = bgr3i(round((0xFFF*(1-opacity))*target_linear + (0xFFF*opacity)*source_linear));
 target_sRGB = byte4(sRGB_forward[linearBlend[0]], sRGB_forward[linearBlend[1]], sRGB_forward[linearBlend[2]],
   min(0xFF,target_sRGB.a+int(round(0xFF*opacity)))); // Additive opacity accumulation
}


static void fill(uint* target, uint stride, uint w, uint h, uint value) {
 for(uint y=0; y<h; y++) {
  for(uint x=0; x<w; x++) target[x] = value;
  target += stride;
 }
}

void fill(const Image& target, int2 origin, uint2 size, bgr3f color, float opacity) {
 assert_(bgr3f(0) <= color && color <= bgr3f(1));

 int2 min = ::max(int2(0), origin);
 int2 max = ::min(int2(target.size), origin+int2(size));
 if(max<=min) return;

 if(opacity==1) { // Solid fill
  if(!(min < max)) return;
  bgr3i linear = bgr3i(round(float(0xFFF)*color));
  byte4 sRGB = byte4(sRGB_forward[linear[0]], sRGB_forward[linear[1]], sRGB_forward[linear[2]], 0xFF);
  fill((uint*)target.data+min.y*target.stride+min.x, target.stride, max.x-min.x, max.y-min.y, (uint&)sRGB);
 } else {
  for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) blend(target, x, y, color, opacity);
 }
}

void blit(const Image& target, int2 origin, const Image& source, bgr3f color, float opacity) {
 assert_(bgr3f(0) <= color && color <= bgr3f(1));
 assert_(source);

 int2 min = ::max(int2(0), origin);
 int2 max = ::min(int2(target.size), origin+int2(source.size));
 /**/  if(color==bgr3f(1) && opacity==1 && !source.alpha) { // Copy
  for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
   byte4 s = source(x-origin.x, y-origin.y);
   target(x,y) = byte4(s[0], s[1], s[2], 0xFF);
  }
 }
 else if(color==bgr3f(0) && opacity==1) { // Alpha multiply (e.g. glyphs)
  for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
   int opacity = source(x-origin.x,y-origin.y).a; // FIXME: single channel images
   byte4& target_sRGB = target(x,y);
   vec3 target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
   int3 linearBlend = int3( round((0xFFF*(1-float(opacity)/0xFF))
                                  * vec3(target_linear)));
   target_sRGB = byte4(sRGB_forward[linearBlend[0]],
     sRGB_forward[linearBlend[1]],
     sRGB_forward[linearBlend[2]],
     ::min(0xFF,int(target_sRGB.a)+opacity)); // Additive opacity accumulation
  }
 }
 else {
  for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
   byte4 BGRA = source(x-origin.x,y-origin.y);
   bgr3f linear = bgr3f(sRGB_reverse[BGRA[0]], sRGB_reverse[BGRA[1]], sRGB_reverse[BGRA[2]]);
   blend(target, x, y, color*linear, opacity*BGRA.a/0xFF);
  }
 }
}


static void blend(const Image& target, uint x, uint y, bgr3f color, float opacity, bool transpose) {
 if(transpose) swap(x,y);
 if(x >= uint(target.size.x) || y >= uint(target.size.y)) return;
 blend(target, x,y, color, opacity);
}

void line(const Image& target, vec2 p1, vec2 p2, bgr3f color, float opacity, bool hint) {
 assert_(isNumber(p1) && isNumber(p2));
 //if(hint && p1.y == p2.y) p1.y = p2.y = round(p1.y); // Hints
 if(hint) { // TODO: preprocess
  if(p1.x == p2.x) fill(target, int2(round(p1)), uint2(1, p2.y-p1.y), color, opacity);
  else if(p1.y == p2.y) fill(target, int2(round(p1)), uint2(p2.x-p1.x, 1), color, opacity);
  else error("");
  return;
 }
 //if(p1.x >= target.size.x || p2.x < 0) return; // Assumes p1.x < p2.x
 assert(bgr3f(0) <= color && color <= bgr3f(1));

 float dx = p2.x - p1.x, dy = p2.y - p1.y;
 bool transpose=false;
 if(abs(dx) < abs(dy)) { swap(p1.x, p1.y); swap(p2.x, p2.y); swap(dx, dy); transpose=true; }
 if(p1.x > p2.x) { swap(p1.x, p2.x); swap(p1.y, p2.y); }
 if(dx==0) return; //p1==p2
 float gradient = dy / dx;
 int i1; float intery;
 {
  float xend = round(p1.x), yend = p1.y + gradient * (xend - p1.x);
  float xgap = 1 - fract(p1.x + 1./2);
  blend(target, xend, yend, color, (1-fract(yend)) * xgap * opacity, transpose);
  blend(target, xend, yend+1, color, fract(yend) * xgap * opacity, transpose);
  i1 = int(xend);
  intery = yend + gradient;
 }
 int i2;
 {
  float xend = round(p2.x), yend = p2.y + gradient * (xend - p2.x);
  float xgap = fract(p2.x + 1./2);
  blend(target, xend, yend, color, (1-fract(yend)) * xgap * opacity, transpose);
  blend(target, xend, yend+1, color, fract(yend) * xgap * opacity, transpose);
  i2 = int(xend);
 }
 int x = i1+1;
 if(x < 0) { intery += (0-x) * gradient; x = 0; }
 for(;x<min(int(transpose ? target.size.y : target.size.x), i2); x++) {
  blend(target, x, intery, color, (1-fract(intery)) * opacity, transpose);
  blend(target, x, intery+1, color, fract(intery) * opacity, transpose);
  intery += gradient;
 }
}

static void trapezoidX(const Image& target, TrapezoidX::Span s0, TrapezoidX::Span s1, bgr3f color, float opacity) {
 if(s0.y > s1.y) swap(s0, s1);
 for(uint y: range(max(0, int(s0.y)), min(int(target.size.y), int(s1.y)))) {
  float x0 = float(s0.min) + float((s1.min - s0.min) * int(y - s0.y)) / float(s1.y - s0.y); // FIXME: step
  float f0 = floor(x0);
  int i0 = int(f0);
  float x1 = float(s0.max) + float((s1.max - s0.max) * int(y - s0.y)) / float(s1.y - s0.y); // FIXME: step
  float f1 = floor(x1);
  int i1 = int(f1);
  if(uint(i0) < uint(target.size.x)) blend(target, i0, y, color, opacity*(1-(x0-f0)));
  for(uint x: range(max(0,i0+1), min(int(target.size.x),i1))) { // FIXME: clip once
   blend(target, x,y, color, opacity); // FIXME: antialias first last column
  }
  if(uint(i1) < uint(target.size.x)) blend(target, i1, y, color, opacity*(x1-f1));
 }
}

static void trapezoidY(const Image& target, TrapezoidY::Span s0, TrapezoidY::Span s1, bgr3f color, float opacity) {
 if(s0.x > s1.x) swap(s0, s1);
 for(uint x: range(max(0, int(s0.x)), min(int(target.size.x), int(s1.x)))) {
  float y0 = float(s0.min) + float((s1.min - s0.min) * int(x - s0.x)) / float(s1.x - s0.x); // FIXME: step
  float f0 = floor(y0);
  int i0 = int(f0);
  float y1 = float(s0.max) + float((s1.max - s0.max) * int(x - s0.x)) / float(s1.x - s0.x); // FIXME: step
  float f1 = floor(y1);
  int i1 = int(f1);
  if(uint(i0)<target.size.y) blend(target, x, i0, color, opacity*(1-(y0-f0)));
  for(uint y: range(max(0,i0+1), min(int(target.size.y),i1))) { // FIXME: clip once
   blend(target, x,y, color, opacity); // FIXME: antialias first last column
  }
  if(uint(i1)<target.size.y) blend(target, x, i1, color, opacity*(y1-f1));
 }
}

/*// 8bit signed integer (for edge flags)
struct Image8 {
 Image8(uint width, uint height) : width(width), height(height) {
  assert(width); assert(height);
  data = ::buffer<int8>(height*width);
  data.clear(0);
 }
 int8& operator()(uint x, uint y) {assert(x<width && y<height, x, y, width, height); return data[y*width+x]; }
 buffer<int8> data;
 uint width, height;
};*/
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

static vec2 cubic(vec2 A, vec2 B, vec2 C, vec2 D, float t) { return ((1-t)*(1-t)*(1-t))*A + (3*(1-t)*(1-t)*t)*B + (3*(1-t)*t*t)*C + (t*t*t)*D; }
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
void cubic(const Image& target, ref<vec2> sourcePoints, bgr3f color, float opacity, vec2 offset, const uint oversample=4) {
 byte points_[sourcePoints.size*sizeof(vec2)]; mref<vec2> points ((vec2*)points_, sourcePoints.size); //FIXME: stack<T> points(sourceSize.size)
 for(size_t index: range(sourcePoints.size)) points[index] = offset+sourcePoints[index];
 vec2 pMin = vec2(target.size), pMax = 0;
 for(vec2 p: points) pMin = ::min(pMin, p), pMax = ::max(pMax, p);
 pMin = floor(pMin), pMax = ceil(pMax);
 const int2 iMin = int2(pMin), iMax = int2(pMax);
 const int2 cMin = max(int2(0),iMin), cMax = min(int2(target.size), iMax);
 if(!(cMin < cMax)) return;
 const int2 size = iMax-iMin;
 Image8 raster(oversample*size.x+1,oversample*size.y+1);
 lastStepY = 0;
 for(uint i=0;i<points.size; i+=3) {
  cubic(raster, float(oversample)*(points[i]-pMin),
        float(oversample)*(points[(i+1)%points.size]-pMin),
    float(oversample)*(points[(i+2)%points.size]-pMin),
    float(oversample)*(points[(i+3)%points.size]-pMin) );
 }
 for(uint y: range(cMin.y, cMax.y)) {
  int acc[oversample]; mref<int>(acc,oversample).clear(0);
  for(uint x: range(iMin.x, cMin.x)) for(uint j: range(oversample)) for(uint i: range(oversample))
   acc[j] += raster((x-iMin.x)*oversample+i, (y-iMin.y)*oversample+j);
  for(uint x: range(cMin.x, cMax.x)) { //Supersampled rasterization
   int coverage = 0;
   for(uint j: range(oversample)) for(uint i: range(oversample)) {
    acc[j] += raster((x-iMin.x)*oversample+i, (y-iMin.y)*oversample+j);
    coverage += acc[j]!=0;
   }
   if(coverage) blend(target, x, y, color, float(coverage)/sq(oversample)*opacity);
  }
 }
}

void render(const Image& target, const Graphics& graphics, vec2 offset) {
 offset += graphics.offset;
 for(const auto& e: graphics.blits) {
  if(int2(e.size) == int2(e.image.size))
   blit(target, int2(round(offset+e.origin)), e.image, e.color, e.opacity);
  //else blit(target, int2(round(offset+e.origin)), resize(int2(round(e.size)), e.image), e.color, e.opacity); // FIXME: subpixel blit
 }
 for(const auto& e: graphics.fills) fill(target, int2(round(offset+e.origin)), uint2(e.size), e.color, e.opacity);
 for(const auto& e: graphics.lines) line(target, offset+e.a, offset+e.b, e.color, e.opacity, e.hint);
 for(const auto& e: graphics.glyphs) {
  Font::Glyph glyph = e.font.font(e.fontSize).render(e.index);
  if(glyph.image) blit(target, int2(round(offset+e.origin))+glyph.offset, glyph.image, e.color, e.opacity);
 }
 for(TrapezoidX e: graphics.trapezoidsX) {
  for(auto& span: e.span) span.y += offset.y, span.min += offset.x, span.max += offset.x;
  trapezoidX(target, e.span[0], e.span[1], e.color, e.opacity);
 }
 for(TrapezoidY e: graphics.trapezoidsY) {
  for(auto& span: e.span) span.x += offset.x, span.min += offset.y, span.max += offset.y;
  trapezoidY(target, e.span[0], e.span[1], e.color, e.opacity);
 }
 for(const auto& e: graphics.cubics) cubic(target, e.points, e.color, e.opacity, offset);
 for(const auto& e: graphics.graphics) {
  assert_(isNumber(e.key));
  render(target, e.value, offset+e.key);
 }
}
