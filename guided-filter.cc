#include "guided-filter.h"

map<string, Time> times;

void guidedFilter(const Image& target, const Image8& Y, const Image8& U, const Image8& V) {
 times["toFloat"].start();
 assert(Y.size == U.size && Y.size == V.size);
 const ImageF I[] {toFloat(Y), toFloat(U), toFloat(V)};
 const int2 size = I[0].size;
 times["toFloat"].stop();

 /*ImageF linear = ::linear(source);
 //ImageF blur = unsafeRef(linear);
 ImageF blur = gaussianBlur(linear, this->R);
 //ImageF mask = apply(mode, blur, [](float a, float b){ return abs(a-b); });
 //ImageF mask = apply(mode, blur, [](float a, float b){ return sq(a-b); });
 ImageF mask = apply(mode, blur, [](float a, float b){ return abs(a-b) > 1./16; });*/

 const int R = 16;
 const float e = 256;
 const ImageF buffer {size.yx()};
 const ImageF meanI[3] {size, size, size};
 const ImageF corrII[6] {size, size, size, size, size, size};
 const size_t index[3*3] = {0,1,2, 1,3,4, 2,4,5};

 for(size_t i: range(3)) { // meanI, covII
  times["meanI"].start();
  ::mean(meanI[i], buffer, I[i], R);
  times["meanI"].stop();
  times["corrII"].start();
  for(size_t j: range(i+1)) {
   const ImageF& corrIIij = corrII[index[i*3+j]];
   for(size_t k: range(corrIIij.ref::size)) corrIIij[k] = I[i][k] * I[j][k]; // corrIIij
   ::mean(corrIIij, buffer, corrIIij, R); // -> mean corrIIij
  }
  times["corrII"].stop();
 }

 const ref<ImageF> p (I);
 const ImageF meanP[3] {size, size, size};
 const ImageF corrIP[3*3] {size,size,size, size,size,size, size,size,size};
 const ImageF a[3*3] {size,size,size, size,size,size, size,size,size};
 const ref<ImageF> b (meanP);

 for(size_t i: range(3)) {
 times["meanP"].start();
 ::mean(meanP[i], buffer, p[i], R);
 times["meanP"].stop();
 times["corrIP"].start();
 for(size_t j: range(3)) {
  const ImageF& corrIPij = corrIP[i*3+j];
  for(size_t k: range(corrIPij.ref::size)) corrIPij[k] = p[i][k] * I[j][k]; // corrIPij
  ::mean(corrIPij, buffer, corrIPij, R); // -> mean corrIPij
 }
 times["corrIP"].stop();
 }
 times["ab"].start();
 for(size_t k: range(a[0].ref::size)) {
  float m00 = corrII[0][k] - meanI[0][k]*meanI[0][k], m01 = corrII[1][k] - meanI[0][k]*meanI[1][k], m02 = corrII[2][k] - meanI[0][k]*meanI[2][k];
  float m11 = corrII[3][k] - meanI[1][k]*meanI[1][k], m12 = corrII[4][k] - meanI[1][k]*meanI[2][k];
  float m22 = corrII[5][k] - meanI[2][k]*meanI[2][k];

  float D = 1/(- m00*m01*m01 + m00*m11*m22
               - m01*m01*m22 + 2*m01*m02*m12
               - m02*m02*m11);

  float a00 = (m22*m11 - m12*m12) * D + e;
  float a01 = (m02*m12 - m22*m01) * D;
  float a02 = (m01*m12 - m02*m11) * D;

  float a11 = (m00*m22 - m02*m02) * D + e;
  float a12 = (m01*m02 - m00*m12) * D;

  float a22 = (m00*m11 - m01*m01) * D + e;

  for(size_t i: range(3)) {
   float meanPi = meanP[i][k];
   float meanI0 = meanI[0][k];
   float meanI1 = meanI[1][k];
   float meanI2 = meanI[2][k];
   float covIPi0 = corrIP[i*3+0][k]  - meanPi*meanI0;
   float covIPi1 = corrIP[i*3+1][k]  - meanPi*meanI1;
   float covIPi2 = corrIP[i*3+2][k]  - meanPi*meanI2;

   float ai0 = a00*covIPi0 + a01*covIPi1 + a02*covIPi2;
   a[i*3+0][k] = ai0;
   float ai1 = a01*covIPi0 + a11*covIPi1 + a12*covIPi2;
   a[i*3+1][k] = ai1;
   float ai2 = a02*covIPi0 + a12*covIPi1 + a22*covIPi2;
   a[i*3+2][k] = ai2;
   b[i][k] = meanPi - (ai0 * meanI0 + ai1 * meanI1 + ai2 * meanI2);
  }
 }
 times["ab"].stop();
 ref<ImageF> meanA (a);
 ref<ImageF> meanB (b);
 ref<ImageF> q (meanB);
 for(size_t i: range(3)) {
  times["meanA"].start();
  for(size_t j: range(3)) ::mean(meanA[i*3+j], buffer, a[i*3+j], R); // -> meanA
  times["meanA"].stop();
  times["meanB"].start();
  ::mean(meanB[i], buffer, b[i], R); // -> meanB
  times["meanB"].stop();
  times["q"].start();
  for(size_t k: range(q[i].ref::size)) q[i][k] = meanB[i][k] + (meanA[i*3+0][k] * I[0][k] + meanA[i*3+1][k] * I[1][k] + meanA[i*3+2][k] * I[2][k]);
  //for(size_t k: range(q[i].ref::size)) q[i][k] = meanA[i*3+0][k];
  times["q"].stop();
 }
 times["sRGB"].start();
 sRGBfromBT709(target, q[0], q[1], q[2]);
 times["sRGB"].stop();
}
