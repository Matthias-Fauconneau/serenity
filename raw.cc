#include "cr2.h"
#include "png.h"
#include "time.h"
#include "matrix.h"
inline double log2(double x) { return __builtin_log2(x); }

/// 2D array of 32bit integer pixels
typedef ImageT<uint32> Image32;

mat3 pseudoinverse(mat3 in) {
 mat3 out;
 float w[3][6];
 for(int i: range(3)) {
  for(int j: range(6)) w[i][j] = (j == i+3);
  for(int j: range(3)) for(int k: range(3))
   w[i][j] += in[k][i] * in[k][j];
 }
 for(int i: range(3)) {
  float sum = w[i][i];
  for(int j: range(6)) w[i][j] /= sum;
  for(int k: range(3)) {
   if (k==i) continue;
   float wki = w[k][i];
   for(int j: range(6)) w[k][j] -= w[i][j] * wki;
  }
 }
 for(int i: range(3)) {
  for(int j: range(3)) {
   out[i][j] = 0;
   for(int k: range(3)) out[i][j] += w[j][k+3] * in[i][k];
  }
 }
 return out;
}


struct Raw {
 Raw() {
  Time decodeTime, totalTime {true};
  size_t totalSize = 0, huffmanSize = 0, entropySize = 0;
  for(string name: Folder(".").list(Files|Sorted))
   if(endsWith(toLower(name), ".cr2")) {
    //log(name);
    Map map(name);
    constexpr bool onlyParse = false;
    decodeTime.start();
    CR2 cr2(map, onlyParse);
    decodeTime.stop();
    totalSize += map.size;
    huffmanSize += cr2.huffmanSize;
    if(onlyParse) continue;
    const Image16& image = cr2.image;
    Image16 planes[4] = {}; // R, G1, G2, B
    for(size_t i: range(4)) planes[i] = Image16(image.size/2);
    for(size_t y: range(image.size.y/2)) for(size_t x: range(image.size.x/2)) {
     planes[0](x,y) = image(x*2+0, y*2+0);
     planes[1](x,y) = image(x*2+1, y*2+0);
     planes[2](x,y) = image(x*2+0, y*2+1);
     planes[3](x,y) = image(x*2+1, y*2+1);
    }
    double entropyCoded = 0;
    for(size_t i: range(4)) {
     const Image16& plane = planes[i];
     int predictor = 0;
     for(int16& value: plane) {
      int next = value-predictor;
      predictor = value;
      assert_(-0x8000 <= next && next <= 0x7FFF, next, value, predictor);
      value = next;
     }
     int16 min = 0x7FFF, max = 0;
     for(int16 value: plane) { min=::min(min, value); max=::max(max, value); }
     //log(min, max);
     assert_(max+1-min <= 59303, min, max, max+1-min);
     buffer<uint32> histogram(max+1-min);
     histogram.clear(0);
     uint32* base = histogram.begin()-min;
     for(int16 value: plane) base[value]++;
     //uint32 maxCount = 0; for(uint32 count: histogram) maxCount=::max(maxCount, count); log(maxCount);
     const uint32 total = plane.ref::size;
     //log("Uniform", str(total*log2(double(max-min))/8/1024/1024, 0u), "MB");
     for(uint32 count: histogram) if(count) entropyCoded += count * log2(double(total)/double(count));
     //entropyCoded += histogram.size*32*8; //
    }
    assert_(entropyCoded/8 <= huffmanSize, entropyCoded/8/1024/1024, huffmanSize/1024/1024);
    log(name, map.size/1024/1024,"MB","Huffman",cr2.huffmanSize/1024/1024,"MB","Entropy", str(entropyCoded/8/1024/1024,0u),"MB");
    entropySize += entropyCoded/8;

    //break;
    log(str(totalSize/1024/1024)+"MB -", str(entropySize/1024/1024)+"MB =", str((totalSize-entropySize)/1024/1024)+"MB ("+str(100*(totalSize-entropySize)/totalSize)+"%");
   }
  log(totalTime);
  log(totalSize/1024/1024,"MB -", compressedSize/1024/1024,"MB =", (totalSize-compressedSize)/1024/1024,"MB", 100*(totalSize-compressedSize)/compressedSize,"%");
  //log(huffmanSize/1024/1024,"MB -", entropySize/1024/1024,"MB =", (huffmanSize-entropySize)/1024/1024,"MB", 100*(huffmanSize-entropySize)/totalSize,"%");
  if(totalSize) log(totalSize/1024/1024,"MB -", entropySize/1024/1024,"MB =", (totalSize-entropySize)/1024/1024,"MB", 100*(totalSize-entropySize)/totalSize,"%");
  }
} app;
