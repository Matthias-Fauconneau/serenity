#include "cr2.h"
#include "time.h"
#include "range.h"
inline double log2(double x) { return __builtin_log2(x); }

struct Raw {
 Raw() {
  size_t imageCount = 0;
  for(string name: Folder(".").list(Files|Sorted)) if(endsWith(toLower(name), ".cr2")) imageCount++;
  Time decodeTime, encodeTime, totalTime {true};
  size_t totalSize = 0, huffmanSize = 0, entropySize = 0;
  size_t index = 0;
  for(string name: Folder(".").list(Files|Sorted)) {
   if(!endsWith(toLower(name), ".cr2")) continue;
   log(name);
   Map map(name);
   constexpr bool onlyParse = false;
   decodeTime.start();
   CR2 cr2(map, onlyParse);
   decodeTime.stop();
   log(decodeTime);
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
   index++;
   log(str(index,3u)+"/"+str(imageCount,3u), name, map.size/1024/1024,"MB","Huffman",cr2.huffmanSize/1024/1024,"MB");
   double entropyCoded = 0;
   for(size_t i: range(4)) {
    const Image16& plane = planes[i];
    Image16 residual (plane.size);
    for(int y: range(plane.size.y)) for(int x: range(plane.size.x)) {
     int left = x>0 ? plane(x-1, y) : 0;
     int top = y>0 ? plane(x, y-1) : 0;
     int predictor = (left+top)/2;
     int value = plane(x, y);
     int r = value - predictor;
     assert_(-0x8000 <= r && r <= 0x7FFF);
     residual(x, y) = r;
    }
    int16 min = 0x7FFF, max = 0;
    for(int16 value: residual) { min=::min(min, value); max=::max(max, value); }
    assert_(max+1-min <= 4096, min, max, max+1-min);
    buffer<uint32> histogram(max+1-min);
    histogram.clear(0);
    uint32* base = histogram.begin()-min;
    for(int16 value: residual) base[value]++;
    uint32 maxCount = 0; for(uint32 count: histogram) maxCount=::max(maxCount, count); log(maxCount, log2(maxCount));
    const uint32 total = residual.ref::size;
    double planeEntropyCoded = 0;
    for(uint32 count: histogram) if(count) planeEntropyCoded += count * log2(double(total)/double(count));
    /*for(uint v: range(histogram.size)) {
     if(!histogram[v]) continue;
     int s = v-min;
     uint u  = (s<<1) ^ (s>>31); // s>0 ? (s<<1) : (-s<<1) - 1;
     // Exp Golomb _ r
     static constexpr size_t EG = 10;
     uint x = u + (1<<EG);
     uint b = (sizeof(x)*8-1) - __builtin_clz(x); // BSR
     assert_((sizeof(x)*8-1) >= __builtin_clz(x), (sizeof(x)*8-1), __builtin_clz(x), x, u, (1<<EG));
     assert_(b+b+1 > EG);
     assert_(b+b+1-EG <= 16, b+b+1-EG, b, EG, u, histogram[v]);
     entropyCoded += histogram[v] * (b+b+1-EG);
    }*/
    log(histogram.size*32, str(100*histogram.size*32/planeEntropyCoded)+"%");
    planeEntropyCoded += histogram.size * 32; // 10K
    entropyCoded += planeEntropyCoded;
#if 0
    for(int method: range(3)) {
     float encodedSize = 0;
     if(method==0) for(uint32 count: histogram) encodedSize += count ? 20 : 0;
     if(method==1) {
      int zeroLength = 0;
      for(uint32 count: histogram) {
       if(!count) { zeroLength++; continue; }
       if(zeroLength) { encodedSize += 19+12; /*escape, length*/  zeroLength=0; }
       else encodedSize += 19;
      }
     }
     if(method==2) for(uint32 count: histogram) encodedSize += 2*log2(1+count)+1; // Golomb
     log(encodedSize);
    }
#endif
   }
   //assert_(entropyCoded/8 <= huffmanSize, entropyCoded/8/1024/1024, huffmanSize/1024/1024);
   entropySize += entropyCoded/8;
   log(str(entropyCoded/8/1024/1024,0u)+"MB",
       "Σ", str(entropySize/1024/1024)+"MB =", str((totalSize-entropySize)/1024/1024)+"MB ("+str(100*(totalSize-entropySize)/totalSize)+"%)");
   break;
  }
  log(decodeTime, totalTime); // 5min
  if(totalSize) log(totalSize/1024/1024,"MB -", huffmanSize/1024/1024,"MB =", (totalSize-huffmanSize)/1024/1024,"MB", 100*(totalSize-huffmanSize)/totalSize,"%");
  if(huffmanSize)
   log(huffmanSize/1024/1024,"MB -", entropySize/1024/1024,"MB =", (huffmanSize-entropySize)/1024/1024,"MB", 100*(huffmanSize-entropySize)/huffmanSize,"%");
  if(totalSize) log(totalSize/1024/1024,"MB -", entropySize/1024/1024,"MB =", (totalSize-entropySize)/1024/1024,"MB", 100*(totalSize-entropySize)/totalSize,"%");
 }
} app;
