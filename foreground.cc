#include "matrix.h"
#include "window.h"
#include "png.h"
#include "guided-filter.h"
constexpr double PI = 3.14159265358979323846; // math.h
inline double exp(float x) { return __builtin_expf(x); } // math.h

struct Foreground : Widget {
 string baseName = arguments()[0];
 int2 size;

 size_t clipFrameIndex = 0, clipFrameCount = 0;
 Map clip[3];

 unique<Window> window = nullptr;
 //bool toggle = false;

 Time totalTime {true};

 ImageF trimap;

 static constexpr int K = 4;
 struct Model {
  struct Component {
   float weight = 1.f/K;
   vec3 mean;
   float a;
   float invV[6]; // inverse of covariance
  } components[K];
 };
 Model models[2];

 //Image debug;

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
  if(0) {
   writeFile("first.png", encodePNG(sRGBfromBT709(first[0], first[1], first[2])));
   return;
  }

  trimap = ImageF(size/2);
  assert_(trimap.size == input.size);
  for(size_t k: range(input.ref::size)) {
   /***/ if(input[k]==byte4(0,0,0xFF)) trimap[k] = 0;
   else if(input[k]==byte4(0,0xFF,0)) trimap[k] = 1;
   else trimap[k] = 1./2;
  }
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
  for(size_t i: range(2)) {
   ref<buffer<uint8>> X (samples[i]);
   size_t N = X[0].size;
   Model model;
   for(size_t k: range(K)) {
    size_t n = random%N;
    model.components[k].mean = vec3(X[0][n], X[1][n], X[2][n]);
   }
   // K-means
   for(size_t unused step: range(12)) {
    // Expectation
    int3 sum[K] = {}; uint count[K]={};
    for(size_t n: range(N)) {
     int3 x (X[0][n], X[1][n], X[2][n]);
     size_t nearestComponent; float distance = inf;
     for(size_t k: range(K)) {
      float d = sq(x - int3(model.components[k].mean));
      if(d < distance) { distance=d; nearestComponent=k; }
     }
     sum[nearestComponent] += x;
     count[nearestComponent] += 1;
    }
    // Maximization
    bool changed = false;
    for(size_t k: range(K)) {
     Model::Component& c = model.components[k];
     vec3 mean = vec3(sum[k]) / float(count[k]);
     if(c.mean != mean) { c.mean = mean; changed = true; }
    }
    if(!changed) break;
   }
   // GMM EM
   // Initializes GMM with cluster covariance (FIXME: only on last step)
   float sumPxx[K][6]; uint count[K]={};
   for(size_t k: range(K)) mref<float>(sumPxx[k]).clear(0);
   for(size_t n: range(N)) {
    int3 x (X[0][n], X[1][n], X[2][n]);
    size_t nearestComponent; float distance = inf;
    for(size_t k: range(K)) {
     float d = sq(x - int3(model.components[k].mean));
     if(d < distance) { distance=d; nearestComponent=k; }
    }
    size_t b = nearestComponent;
    vec3 xm = vec3(x) - model.components[b].mean;
    sumPxx[b][0] += xm[0] * xm[0];
    sumPxx[b][1] += xm[0] * xm[1];
    sumPxx[b][2] += xm[0] * xm[2];
    sumPxx[b][3] += xm[1] * xm[1];
    sumPxx[b][4] += xm[1] * xm[2];
    sumPxx[b][5] += xm[2] * xm[2];
    count[b] += 1;
   }
   for(size_t k: range(K)) {
    Model::Component& c = model.components[k];
    c.weight = float(count[k]) / float(N);
    float m00 = sumPxx[k][0] / count[k];
    float m01 = sumPxx[k][1] / count[k];
    float m02 = sumPxx[k][2] / count[k];
    float m11 = sumPxx[k][3] / count[k];
    float m12 = sumPxx[k][4] / count[k];
    float m22 = sumPxx[k][5] / count[k];
    float D =
      m00 * (m11*m22 - m12*m12) -
      m01 * (m01*m22 - m02*m12) +
      m02 * (m01*m12 - m02*m11);
    assert(D>0);
    c.a = c.weight/sqrt(cb(2*PI)*D);
    float invD = 1/D;
    c.invV[0] =  (m11*m22 - m12*m12) * invD;
    c.invV[1] = -(m01*m22 - m02*m12) * invD;
    c.invV[2] =  (m01*m12 - m02*m11) * invD;
    c.invV[3] =  (m00*m22 - m02*m02) * invD;
    c.invV[4] = -(m00*m12 - m02*m01) * invD;
    c.invV[5] =  (m00*m11 - m01*m01) * invD;
   }

   for(size_t unused step: range(1)) {
    // Expectation
    float sumP[K] = {};
    vec3 sumPx[K] = {};
    buffer<float> Pkx[K];
    for(size_t k: range(K)) Pkx[k] = buffer<float>(N);
    for(size_t n: range(N)) {
     vec3 x (X[0][n], X[1][n], X[2][n]);
     float Pxk[K]; float sumKPxk = 0;
     for(size_t k: range(K)) {
      Model::Component c = model.components[k];
      vec3 xm = x - c.mean;
      vec3 invVxm (c.invV[0] * xm[0] + c.invV[1] * xm[1] + c.invV[2] * xm[2], c.invV[1] * xm[0] + c.invV[3] * xm[1] + c.invV[4] * xm[2], c.invV[2] * xm[0] + c.invV[4] * xm[1] + c.invV[5] * xm[2]);
      Pxk[k] = c.a * exp( -1.f/2 * dot(xm, invVxm));
      sumKPxk += Pxk[k];
     }
     for(size_t k: range(K)) {
      Pkx[k][n] = Pxk[k] / sumKPxk;
      sumP[k] += Pkx[k][n];
      sumPx[k] += Pkx[k][n] * x;
     }
    }
    // Maximization
    float sumPxx[K][6];
    for(size_t k: range(K)) {
     Model::Component& c = model.components[k];
     c.weight = sumP[k] / N;
     c.mean = sumPx[k] / sumP[k];
     mref<float>(sumPxx[k]).clear(0);
    }
    for(size_t n: range(N)) {
     vec3 x (X[0][n], X[1][n], X[2][n]);
     for(size_t k: range(K)) {
      vec3 m = model.components[k].mean;
      vec3 xm = x - m;
      float P = Pkx[k][n];
      sumPxx[k][0] += P * xm[0] * xm[0];
      sumPxx[k][1] += P * xm[0] * xm[1];
      sumPxx[k][2] += P * xm[0] * xm[2];
      sumPxx[k][3] += P * xm[1] * xm[1];
      sumPxx[k][4] += P * xm[1] * xm[2];
      sumPxx[k][5] += P * xm[2] * xm[2];
     }
    }
    for(size_t k: range(K)) {
     Model::Component& c = model.components[k];
     float m00 = sumPxx[k][0] / sumP[k];
     float m01 = sumPxx[k][1] / sumP[k];
     float m02 = sumPxx[k][2] / sumP[k];
     float m11 = sumPxx[k][3] / sumP[k];
     float m12 = sumPxx[k][4] / sumP[k];
     float m22 = sumPxx[k][5] / sumP[k];
     float D =
       m00 * (m11*m22 - m12*m12) -
       m01 * (m01*m22 - m02*m12) +
       m02 * (m01*m12 - m02*m11);
     c.a = c.weight/sqrt(cb(2*PI)*D);
     float invD = 1/D;
     c.invV[0] =  (m11*m22 - m12*m12) * invD;
     c.invV[1] = -(m01*m22 - m02*m12) * invD;
     c.invV[2] =  (m01*m12 - m02*m11) * invD;
     c.invV[3] =  (m00*m22 - m02*m02) * invD;
     c.invV[4] = -(m00*m12 - m02*m01) * invD;
     c.invV[5] =  (m00*m11 - m01*m01) * invD;
    }
   }
   models[i] = model;
  }

#if 0
  debug = Image(256*int2(3,2)); debug.clear(byte4(0,0,0,0xFF)); // Plots color distribution as additive projection orthogonal to each Y,U,V axis
  Image view[2][3]; for(size_t i: range(2)) for(size_t p: range(3)) view[i][p] = cropRef(debug, int2(p, i)*int2(256), int2(256));
  for(size_t i: range(2)) {
   ref<buffer<uint8>> X (samples[i]);
   ImageF PX[3][3];
   for(size_t p: range(3)) for(size_t c: range(3)) { PX[p][c] = ImageF(256); PX[p][c].clear(0); }
   for(size_t n: range(X[0].size)) {
    int3 x (X[0][n], X[1][n], X[2][n]);
    for(size_t p: range(3)) {
     int2 Ps (x[p], x[(p+1)%3]);
     float Pxk[K]; float sumKPxk = 0;
     for(size_t k: range(K)) {
      Model::Component c = models[i].components[k];
      vec3 xm = vec3(x) - c.mean;
      vec3 invVxm (c.invV[0] * xm[0] + c.invV[1] * xm[1] + c.invV[2] * xm[2], c.invV[1] * xm[0] + c.invV[3] * xm[1] + c.invV[4] * xm[2], c.invV[2] * xm[0] + c.invV[4] * xm[1] + c.invV[5] * xm[2]);
      Pxk[k] = c.a * exp( -1.f/2 * dot(xm, invVxm));
      sumKPxk += Pxk[k];
     }
     bgr3f colors[K] = {vec3(1,0,0),vec3(0,1,0),vec3(0,0,1),vec3(1./2,1./2,0)};
     bgr3f color = 0;
     for(size_t k: range(K)) color += Pxk[k] / sumKPxk * colors[k];
     for(size_t c: range(3)) PX[p][c](Ps.x, Ps.y) += color[c];
    }
   }
   for(size_t p: range(3)) for(size_t y: range(view[i][p].height)) for(size_t x: range(view[i][p].width)) view[i][p](x, y) = byte3(min(bgr3i(0xFF), bgr3i(PX[p][0](x, y), PX[p][1](x, y), PX[p][2](x, y))));
  }
#endif
  next();

  window = ::window(this, size);
  window->backgroundColor = nan;
  window->presentComplete = [this]{ next(); window->render(); };
  //window->actions[Space] = [this] { toggle=!toggle; };
  //window->actions[Space] = [this] { /*clipFrameIndex++;*/next(); window->render(); };
 }

 Image8 source(size_t component, size_t clipFrameIndex) {
  int2 size = this->size / (component?2:1);
  return Image8(unsafeRef(cast<uint8>(clip[component].slice(clipFrameIndex*(size.x*size.y), size.x*size.y))), size);
 }

 Image image;
 //Image image(size_t clipFrameIndex) {
 void next() {
  Image8 A[] {downsample(source(0, clipFrameIndex)), source(1, clipFrameIndex), source(2, clipFrameIndex)};
  ImageF X[] {toFloat(A[0]), toFloat(A[1]), toFloat(A[2])};
  ImageF P (X[0].size);
  //if(1) P.clear(1./2); else
  for(size_t n: range(X[0].ref::size)) {
   vec3 x (X[0][n], X[1][n], X[2][n]);
   float Pxi[2];
   for(size_t i: range(2)) {
    float Px = 0;
    for(size_t k: range(K)) {
      Model::Component c = models[i].components[k];
      vec3 xm = x - c.mean;
      vec3 invVxm (c.invV[0] * xm[0] + c.invV[1] * xm[1] + c.invV[2] * xm[2], c.invV[1] * xm[0] + c.invV[3] * xm[1] + c.invV[4] * xm[2], c.invV[2] * xm[0] + c.invV[4] * xm[1] + c.invV[5] * xm[2]);
      Px += c.a * exp( -1.f/2 * dot(xm, invVxm) );
    }
    Pxi[i] = Px;
   }
   P[n] = Pxi[1]/(Pxi[0]+Pxi[1]);
  }
  for(size_t n: range(X[0].ref::size)) { if(trimap[n] == 0) P[n]=0; else if(trimap[n] == 1) P[n]=1; } // Known classification from previous frame (or initial input) (TODO: soft)
  //if(1) P = gaussianBlur(P, 4); else
  P = guidedFilter(ref<ImageF>(X), P, 8, sq(8)); // Edge aware blur
  for(size_t n: range(X[0].ref::size)) P[n] = P[n] > 1./2; // Threshold
  P = ::mean(P, 2); // Fill holes
  for(size_t n: range(X[0].ref::size)) P[n] = P[n] > 1./2; // Threshold
  // Initializes known classification for next frame
  ImageF soft = ::gaussianBlur(P, 16);
  clipFrameIndex++;
  if(clipFrameIndex < clipFrameCount) {
     Image8 B[] {downsample(source(0, clipFrameIndex)), source(1, clipFrameIndex), source(2, clipFrameIndex)};
     for(size_t n: range(X[0].ref::size)) {
      int d = sq(int(A[0][n])-int(B[0][n])) + sq(int(A[1][n])-int(B[1][n])) + sq(int(A[2][n])-int(B[2][n]));
      if(d > sq(8) && soft[n]>1./16 && soft[n]<1-1./16) trimap[n] = 1./2;
      else trimap[n] = P[n];
     }
  }
  P = ::mean(P, 2); // Feathers
  for(size_t n: range(X[0].ref::size)) {
   float p = P[n];
   X[0][n] *= p;
   X[1][n] = 128+(X[1][n]-128)*p;
   X[2][n] = 128+(X[2][n]-128)*p;
  }
  image = sRGBfromBT709(X[0], X[1], X[2]);
  //return sRGBfromBT709(X[0], X[1], X[2]);
 }

 vec2 sizeHint(vec2) override { return vec2(size/2); }

 shared<Graphics> graphics(vec2) override {
  if(clipFrameIndex >= clipFrameCount) { requestTermination(); return shared<Graphics>(); }
  //resize(window->target, image(clipFrameIndex));
  resize(window->target, image);
  //clipFrameIndex++;
  return shared<Graphics>();
 }
} app;
