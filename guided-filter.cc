#include "guided-filter.h"

map<string, Time> times;

void guidedFilter(const ImageF& q, const ImageF& I, const ImageF& p, const int R, const float e) {
 const int2 size = I.size;

 const ImageF buffer {size.yx()};
 const ImageF meanI {size};
 const ImageF corrII {size};

 times["meanI"].start();
 ::mean(meanI, buffer, I, R);
 times["meanI"].stop();
 times["corrII"].start();
 for(size_t k: range(corrII.ref::size)) corrII[k] = I[k] * I[k];
 ::mean(corrII, buffer, corrII, R);
 times["corrII"].stop();

 const ImageF& meanP = q;
 const ImageF corrIP {size};
 const ImageF a {size};
 const ImageF& b = meanP;

 times["meanP"].start();
 ::mean(meanP, buffer, p, R);
 times["meanP"].stop();
 times["corrIP"].start();
 for(size_t k: range(corrIP.ref::size)) corrIP[k] = p[k] * I[k];
 ::mean(corrIP, buffer, corrIP, R);
 times["corrIP"].stop();
 times["ab"].start();
 for(size_t k: range(a.ref::size)) {
  float m00 = corrII[0] - meanI[k]*meanI[k] + e;
  float a00 = 1/m00;
  float meanPk = meanP[k];
  float meanIk = meanI[k];
  float covIPk = corrIP[k]  - meanPk*meanIk;
  float ak = a00*covIPk;
  a[k] = ak;
  b[k] = meanPk - (ak * meanIk);
 }
 times["ab"].stop();
 times["meanA"].start();
 ::mean(a, buffer, a, R);
 times["meanA"].stop();
 times["meanB"].start();
 ::mean(b, buffer, b, R);
 times["meanB"].stop();
 times["q"].start();
 for(size_t k: range(q.ref::size)) q[k] = b[k] + (a[k] * I[k]);
 times["q"].stop();
}

void guidedFilter(const ImageF& q, const ref<ImageF> I, const ImageF& p, const int R, const float e) {
 const int2 size = I[0].size;

 const ImageF buffer {size.yx()};
 const ImageF meanI[3] {size, size, size};
 const ImageF corrII[6] {size, size, size, size, size, size};
 const size_t index[3*3] = {0,1,2, 1,3,4, 2,4,5};

 for(size_t i: range(3)) {
  times["meanI"].start();
  ::mean(meanI[i], buffer, I[i], R);
  times["meanI"].stop();
  times["corrII"].start();
  for(size_t j: range(i+1)) {
   const ImageF& corrIIij = corrII[index[i*3+j]];
   for(size_t k: range(corrIIij.ref::size)) corrIIij[k] = I[i][k] * I[j][k];
   ::mean(corrIIij, buffer, corrIIij, R);
  }
  times["corrII"].stop();
 }

 const ImageF& meanP = q;
 const ImageF corrIP[3] {size,size,size};
 const ImageF a[3] {size,size,size};
 const ImageF& b = meanP;

 times["meanP"].start();
 ::mean(meanP, buffer, p, R);
 times["meanP"].stop();
 times["corrIP"].start();
 for(size_t j: range(3)) {
  const ImageF& corrIPj = corrIP[j];
  for(size_t k: range(corrIPj.ref::size)) corrIPj[k] = p[k] * I[j][k];
  ::mean(corrIPj, buffer, corrIPj, R);
 }
 times["corrIP"].stop();
 times["ab"].start();
 for(size_t k: range(a[0].ref::size)) {
  float m00 = corrII[0][k] - meanI[0][k]*meanI[0][k] + e, m01 = corrII[1][k] - meanI[0][k]*meanI[1][k], m02 = corrII[2][k] - meanI[0][k]*meanI[2][k];
  float m11 = corrII[3][k] - meanI[1][k]*meanI[1][k] + e, m12 = corrII[4][k] - meanI[1][k]*meanI[2][k];
  float m22 = corrII[5][k] - meanI[2][k]*meanI[2][k] + e;

  float D = 1/(
     m00 * (m11*m22 - m12*m12) -
     m01 * (m01*m22 - m02*m12) +
     m02 * (m01*m12 - m02*m11) );

  float a00 =  (m11*m22 - m12*m12) * D;
  float a01 = -(m01*m22 - m02*m12) * D;
  float a02 =  (m01*m12 - m02*m11) * D;

  float a11 =  (m00*m22 - m02*m02) * D;
  float a12 = -(m00*m12 - m02*m01) * D;

  float a22 = (m00*m11 - m01*m01) * D;

  float meanPk = meanP[k];
  float meanI0 = meanI[0][k];
  float meanI1 = meanI[1][k];
  float meanI2 = meanI[2][k];
  float covIP0 = corrIP[0][k]  - meanPk*meanI0;
  float covIP1 = corrIP[1][k]  - meanPk*meanI1;
  float covIP2 = corrIP[2][k]  - meanPk*meanI2;

   float ai0 = a00*covIP0 + a01*covIP1 + a02*covIP2;
   a[0][k] = ai0;
   float ai1 = a01*covIP0 + a11*covIP1 + a12*covIP2;
   a[1][k] = ai1;
   float ai2 = a02*covIP0 + a12*covIP1 + a22*covIP2;
   a[2][k] = ai2;
   b[k] = meanPk - (ai0 * meanI0 + ai1 * meanI1 + ai2 * meanI2);
 }
 times["ab"].stop();
 times["meanA"].start();
 for(size_t j: range(3)) ::mean(a[j], buffer, a[j], R);
 times["meanA"].stop();
 times["meanB"].start();
 ::mean(b, buffer, b, R);
 times["meanB"].stop();
 times["q"].start();
 for(size_t k: range(q.ref::size)) q[k] = b[k] + (a[0][k] * I[0][k] + a[1][k] * I[1][k] + a[2][k] * I[2][k]);
 times["q"].stop();
}

void guidedFilter(const ref<ImageF> q, const ref<ImageF> I, const int R, const float e) {
 guidedFilter(q, I, I, R, e); // FIXME: optimize I=p case
}

void guidedFilter(const ref<ImageF> q, const ref<ImageF> I, const ref<ImageF> p, const int R, const float e) {
 const int2 size = I[0].size;

 const ImageF buffer {size.yx()};
 const ImageF meanI[3] {size, size, size};
 const ImageF corrII[6] {size, size, size, size, size, size};
 const size_t index[3*3] = {0,1,2, 1,3,4, 2,4,5};

 for(size_t i: range(3)) {
  times["meanI"].start();
  ::mean(meanI[i], buffer, I[i], R);
  times["meanI"].stop();
  times["corrII"].start();
  for(size_t j: range(i+1)) {
   const ImageF& corrIIij = corrII[index[i*3+j]];
   for(size_t k: range(corrIIij.ref::size)) corrIIij[k] = I[i][k] * I[j][k];
   ::mean(corrIIij, buffer, corrIIij, R);
  }
  times["corrII"].stop();
 }

 const ref<ImageF> meanP (q);
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
  for(size_t k: range(corrIPij.ref::size)) corrIPij[k] = p[i][k] * I[j][k];
  ::mean(corrIPij, buffer, corrIPij, R);
 }
 times["corrIP"].stop();
 }
 times["ab"].start();
 for(size_t k: range(a[0].ref::size)) {
  float m00 = corrII[0][k] - meanI[0][k]*meanI[0][k] + e, m01 = corrII[1][k] - meanI[0][k]*meanI[1][k], m02 = corrII[2][k] - meanI[0][k]*meanI[2][k];
  float m11 = corrII[3][k] - meanI[1][k]*meanI[1][k] + e, m12 = corrII[4][k] - meanI[1][k]*meanI[2][k];
  float m22 = corrII[5][k] - meanI[2][k]*meanI[2][k] + e;

  float D = 1/(
     m00 * (m11*m22 - m12*m12) -
     m01 * (m01*m22 - m02*m12) +
     m02 * (m01*m12 - m02*m11) );

  float a00 =  (m11*m22 - m12*m12) * D;
  float a01 = -(m01*m22 - m02*m12) * D;
  float a02 =  (m01*m12 - m02*m11) * D;

  float a11 =  (m00*m22 - m02*m02) * D;
  float a12 = -(m00*m12 - m02*m01) * D;

  float a22 = (m00*m11 - m01*m01) * D;

  float meanI0 = meanI[0][k];
  float meanI1 = meanI[1][k];
  float meanI2 = meanI[2][k];
  for(size_t i: range(3)) {
   float meanPi = meanP[i][k];
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
 for(size_t i: range(3)) {
  times["meanA"].start();
  for(size_t j: range(3)) ::mean(a[i*3+j], buffer, a[i*3+j], R);
  times["meanA"].stop();
  times["meanB"].start();
  ::mean(b[i], buffer, b[i], R);
  times["meanB"].stop();
  times["q"].start();
  for(size_t k: range(q[i].ref::size)) q[i][k] = b[i][k] + (a[i*3+0][k] * I[0][k] + a[i*3+1][k] * I[1][k] + a[i*3+2][k] * I[2][k]);
  times["q"].stop();
 }
}

ImageF guidedFilter(const ImageF& I, const ImageF& p, const int R, const float e) { ImageF q(p.size); guidedFilter(q, I, p, R, e); return q; }
ImageF guidedFilter(ref<ImageF> I, const ImageF& p, const int R, const float e) { ImageF q(p.size); guidedFilter(q, I, p, R, e); return q; }
