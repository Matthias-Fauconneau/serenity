#include "window.h"
#include "guided-filter.h"

#if 0
void corner(const ImageF& target, const ImageF& source) {
 assert_(target.size == source.size);
 target.clear();
 const int m = 9;
 const int R = 2;
 const int r = 3;
 for(size_t y : range(m+r, source.size.y-m-r)) for(size_t x : range(m+r, source.size.x-m-r)) {
  float min = inf;
  for(int oy: range(-R, R+1)) for(int ox: range(-R, R+1)) { // FIXME: round window
   if(ox==0 && oy==0) continue;
   float sum = 0;
   for(int dy: range(-r, r+1)) for(int dx: range(-r, r+1)) {
    sum += abs(source(x+ox+dx, y+oy+dy) - source(x+dx, y+dy));
   }
   if(sum < min) min = sum;
  }
  target(x,y) = min;
 }
}
ImageF corner(const ImageF& source) { ImageF target(source.size); corner(target, source); return target; }

#if 0
// Filters non maximum
void localMax(const ImageF& target, const ImageF& source, int R) {
 for(size_t y : range(R, source.size.y-R)) for(size_t x : range(R, source.size.x-R)) {
  float max = source(x, y);
  for(int dy: range(-R, R +1)) for(int dx: range(-R, R+1)) if((dx || dy) && source(x+dx, y+dy) >= max) { max=0; break; }
  target(x, y) = max;
 }
}
ImageF localMax(const ImageF& source, int R) { ImageF target(source.size); localMax(target, source, R); return target; }
#else
// Filters non maximum
buffer<int2> localMax(const ImageF& source, int R) {
 buffer<int2> target(2048, 0);
 for(size_t y : range(R, source.size.y-R)) for(size_t x : range(R, source.size.x-R)) { // FIXME: round window
  float max = source(x, y);
  for(int dy: range(-R, R +1)) for(int dx: range(-R, R+1)) if((dx || dy) && source(x+dx, y+dy) >= max) { max=0; break; }
  if(max) {
   assert_(target.size < target.capacity, target.size, target.capacity);
   target.append(int2(x, y));
  }
 }
 return target;
}
#endif

void track(const mref<int2> target, const ImageF& A, const ImageF& B, const ref<int2> source) {
 const int R = 9;
 const int r = 3;
 for(size_t i: range(source.size)) {
  size_t x = source[i].x, y = source[i].y;
  float min = inf; int Ox, Oy;
  for(int oy: range(-R, R+1)) for(int ox: range(-R, R+1)) { // FIXME: round window
   float sum = 0;
   for(int dy: range(-r, r+1)) for(int dx: range(-r, r+1)) {
    sum += abs(B(x+ox+dx, y+oy+dy) - A(x+dx, y+dy));
   }
   sum += sq(ox) + sq(oy); // Regularization
   if(sum < min) min = sum, Ox = ox, Oy = oy;
  }
  target[i] = int2(Ox, Oy);
 }
}
#endif

struct Foreground : Widget {
 string baseName = arguments()[0];
 int2 size;

 size_t clipFrameIndex = 0, clipFrameCount = 0;
 Map clip[3];

 unique<Window> window = nullptr;
 bool toggle = false;

 Time totalTime {true};

 ImageF temporal;

 Foreground() {
  for(string file: currentWorkingDirectory().list(Files)) {
   TextData s(section(file,'.',-3,-2));
   if(startsWith(file, baseName) && s.isInteger() && endsWith(file, ".Y")) {
    int2 size; size.x = s.integer(); s.skip('x'); size.y = s.integer();
    if(!this->size) this->size = size;
    assert_(size == this->size);
   }
  }
  assert_(size);

  size_t clipIndex = 1;
  String name = baseName+'.'+str(clipIndex)+'.'+strx(size);
  for(size_t i: range(3)) clip[i] = Map(name+'.'+"YUV"[i]);
  assert_(clip[0].size % (size.x*size.y) == 0);
  clipFrameCount = clip[0].size / (size.x*size.y);

  temporal = ImageF(size/2); temporal.clear(8);

  window = ::window(this, size);
  window->backgroundColor = nan;
  window->presentComplete = [this]{ window->render(); };
  window->actions[Space] = [this] { toggle=!toggle; };
 }

 Image8 source(size_t component, size_t clipFrameIndex) {
  int2 size = this->size / (component?2:1);
  return Image8(unsafeRef(cast<uint8>(clip[component].slice(clipFrameIndex*(size.x*size.y), size.x*size.y))), size);
 }

 Image image(size_t clipFrameIndex) {
  //Image8 images[] {downsample(this->image(0, clipFrameIndex)), this->image(1, clipFrameIndex), this->image(2, clipFrameIndex)};
  //const ImageF I[] {toFloat(images[0]), toFloat(images[1]), toFloat(images[2])};
  ImageF source = toFloat(downsample(this->source(0, clipFrameIndex)));
#if 0
  ImageF corner = ::corner(source);
  buffer<int2> corners = localMax(corner, 6);
  //float sum = 0; float N = 0; for(size_t k : range(target.ref::size)) if(target[k]) sum+=target[k], N++; float mean = sum / N;
  //for(size_t k : range(target.ref::size)) target[k] = target[k] ? 255 : 0;
  //size_t N = 0; for(size_t k : range(target.ref::size)) if(target[k]) N++; log(N);
  ImageF next = toFloat(downsample(this->source(0, clipFrameIndex+1)));
  buffer<int2> vectors (corners.size);
  track(vectors, source, next, corners);
  for(size_t k: range(source.ref::size)) source[k] = 128+(source[k]-128)/2; // Reduces contrast
  float max = 0 ;
  for(size_t i: range(vectors.size)) {
   float d = sqrt(float(sq(vectors[i].x) + sq(vectors[i].y)));
   if(d > max) max = d;
  }
  for(size_t i: range(vectors.size)) {
   float d = sqrt(float(sq(vectors[i].x) + sq(vectors[i].y)));
   //source(corners[i].x, corners[i].y) = vectors[i].x||vectors[i].y ? 255 : 0;
   int x0 = corners[i].x, y0 = corners[i].y;
   int x1 = corners[i].x+vectors[i].x, y1 = corners[i].y+vectors[i].y;
   int dx = abs(x1-x0), sx = x0<x1 ? 1 : -1;
   int dy = abs(y1-y0), sy = y0<y1 ? 1 : -1;
   int err = (dx>dy ? dx : -dy)/2, e2;

   for(;;) {
    source(x0, y0) = d*255/max;
    if (x0==x1 && y0==y1) break;
    e2 = err;
    if (e2 >-dx) { err -= dy; x0 += sx; }
    if (e2 < dy) { err += dx; y0 += sy; }
   }
  }
#else

  ImageF next = toFloat(downsample(this->source(0, clipFrameIndex+1)));
  ImageF next1 = toFloat(downsample(this->source(0, clipFrameIndex+2)));

  /*float sum0 = 0;
  for(size_t k: range(source.ref::size))  sum0 += source[k];
  float mean0 = sum0 / source.ref::size;

  float sum1 = 0;
  for(size_t k: range(next.ref::size)) sum1 += next[k];
  float mean1 = sum1 / next.ref::size;*/

  //float sumD = 0;
  ImageF contour (source.size);
  for(size_t k: range(source.ref::size)) {
   contour[k] = abs(source[k]-next[k]) + abs(next[k]-next1[k]);
   //float d = //((source[k]/mean0)-(next[k]/mean1));
   //source[k] = 255 * d;
   //sumD += d;
  }

  ImageF region;
  region = ::mean(contour, 15);
  const float dt = 1./16;
  for(size_t k: range(region.ref::size)) temporal[k] = (1-dt)*temporal[k] + dt*region[k];
  for(size_t k: range(region.ref::size)) region[k] = temporal[k]>8;
  if(!toggle) region = guidedFilter(source, region, 15, 1);
  for(size_t k: range(source.ref::size)) source[k] *= region[k]>1./2;
#endif
  return sRGBfromBT709(source);
 }

 vec2 sizeHint(vec2) override { return vec2(size/2); }

 shared<Graphics> graphics(vec2) override {
  if(clipFrameIndex+2 >= clipFrameCount) { requestTermination(); return shared<Graphics>(); }
  resize(window->target, image(clipFrameIndex));
  clipFrameIndex++;
  return shared<Graphics>();
 }
} app;
