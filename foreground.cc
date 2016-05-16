#include "matrix.h"
#include "window.h"
#include "png.h"

struct Foreground : Widget {
 string baseName = arguments()[0];
 int2 size;

 size_t clipFrameIndex = 0, clipFrameCount = 0;
 Map clip[3];

 unique<Window> window = nullptr;
 bool toggle = false;

 Time totalTime {true};

 ImageF trimap;

 Image debug;

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

  const Image8 first[] = {downsample(this->source(0, 0)), this->source(1, 0), this->source(2, 0)};
  Image input = decodeImage(readFile("trimap.png"));
#if 0
  if(0) {
   writeFile("first.png", encodePNG(sRGBfromBT709(first[0], first[1], first[2])));
   return;
  } else {
   #if 0
   trimap = ImageF(size);
   assert(trimap.size == input.size);
   for(size_t k: range(input.ref::size)) {
    /***/ if(input[k]==byte4(0,0,0xFF)) trimap[k] = 0;
    else if(input[k]==byte4(0,0xFF,0)) trimap[k] = 1;
    else trimap[k] = 1./2;
   }
#elif 0
   buffer<uint32> background[3], foreground[3];
   for(size_t i: range(3)) {
    background[i] = buffer<uint32>(256); background[i].clear(0);
    foreground[i] = buffer<uint32>(256); foreground[i].clear(0);
   }
   for(size_t k: range(input.ref::size)) {
    /***/ if(input[k]==byte4(0,0,0xFF)) for(size_t i: range(3)) background[i][first[i][k]]++;
    else if(input[k]==byte4(0,0xFF,0)) for(size_t i: range(3)) foreground[i][first[i][k]]++;
   }
   for(size_t i: range(3)) {
    uint F=0, B=0;
    for(size_t k: range(256)) {
     F += foreground[i][k];
     B += background[i][k];
    }
    if(F < B) for(size_t k: range(256)) foreground[i][k] = foreground[i][k]*F/B;
    else for(size_t k: range(256)) background[i][k] = background[i][k]*F/B;
   }
   trimap = ImageF(size);
   for(size_t k: range(input.ref::size)) {
    /***/ if(input[k]==byte4(0,0,0xFF)) trimap[k] = 0;
    else if(input[k]==byte4(0,0xFF,0)) trimap[k] = 1;
    else {
     int sum = 0;
     for(size_t i: range(3)) sum += foreground[i][first[i][k]] - background[i][first[i][k]];
     if(sum > 0) trimap[k] = 3./4;
     else if(sum < 0) trimap[k] = 1./4;
     else trimap[k] = 1./2;
    }
   }
#endif
#endif
   buffer<uint8> samples[2][3];
   for(size_t i: range(2)) for(size_t j: range(3)) samples[i][j] = buffer<uint8>(input.ref::size, 0);
   // Inlines foreground/background samples from input image into contiguous buffers
   for(size_t k: range(input.ref::size)) {
    size_t i;
    /***/ if(input[k]==byte4(0,0,0xFF)) i=0;
    else if(input[k]==byte4(0,0xFF,0)) i=1;
    else continue;
    for(size_t j: range(3)) samples[i][j].append(first[j][k]);
   }
   Random random;
   static constexpr int K = 1;
   struct Model {
    struct Component {
     vec3 mean;
     mat3 covariance;
    } components[K];
   };
   Model models[2];
   for(size_t i: range(2)) {
    Model model;
    for(size_t k: range(K)) model.components[k].mean = vec3(random(), random(), random());
    for(size_t unused step: range(1)) {
     int3 sum[K] = {}; uint count[K]={};
     for(size_t s: range(samples[i][0].size)) {
      int3 sample (samples[i][0][s], samples[i][1][s], samples[i][2][s]);
      size_t nearestComponent; float distance = inf;
      for(size_t k: range(K)) {
       float d = sq(sample - int3(model.components[k].mean));
       if(d < distance) { distance=d; nearestComponent=k; }
      }
      sum[nearestComponent] += sample;
      count[nearestComponent] += 1;
     }
     for(size_t k: range(K)) model.components[k].mean = vec3(sum[k]) / float(count[k]);
    }
    models[i] = model;
   }

   debug = Image(256*2); debug.clear(byte4(0,0,0,0xFF)); // Plots color distribution as additive projection orthogonal to each Y,U,V axis
   Image view[4]; for(size_t x: range(2)) for(size_t y: range(2)) view[y*2+x] = cropRef(debug, int2(x, y)*debug.size/2, debug.size/2);
   for(size_t i: range(2)) {
    for(size_t s: range(samples[i][0].size)) {
     vec3 sample (samples[i][0][s], samples[i][1][s], samples[i][2][s]);
     for(size_t p: range(3)) {
      int2 Ps (sample[p], sample[(p+1)%3]);
      uint8& bin = view[p](Ps.x, Ps.y)[1+i];
      if(bin<0xFF) bin++;
     }
    }
    for(size_t p: range(3)) {
     for(size_t k: range(K)) {
      vec3 O = models[i].components[k].mean;
      int2 P (O[p], O[(p+1)%3]);
      view[p](P.x, P.y)[0] = 0xFF;
      view[p](P.x, P.y)[1+i] = 0xFF;
     }
    }
   }

  window = ::window(this, size);
  window->backgroundColor = nan;
  //window->presentComplete = [this]{ window->render(); };
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
  for(size_t k: range(source.ref::size)) source[k] *= trimap[k];
  return sRGBfromBT709(source);
 }

 //vec2 sizeHint(vec2) override { return vec2(512); }
 vec2 sizeHint(vec2) override { return vec2(size/2); }

 shared<Graphics> graphics(vec2) override {
  if(clipFrameIndex+2 >= clipFrameCount) { requestTermination(); return shared<Graphics>(); }
  //resize(window->target, image(clipFrameIndex));
  window->target.clear(0);
  resize(cropRef(window->target,0,768), debug);
  //assert_(window->target.size == debug.size, window->target.size, debug.size); copy(window->target, debug);
  //clipFrameIndex++;
  return shared<Graphics>();
 }
} app;
