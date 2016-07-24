#include "cr2.h"
#include "time.h"
inline double log2(double x) { return __builtin_log2(x); }
int median(int a, int b, int c) { return max(min(a,b), min(max(a,b),c)); }

struct Raw {
 Raw() {
  size_t count = 0;
  for(string name: Folder(".").list(Files|Sorted)) if(endsWith(toLower(name), ".cr2")) count++;
  Time decodeTime, encodeTime, totalTime {true};
  const size_t N = 5;
  size_t totalSize = 0, huffmanSize = 0, entropySize[N] = {};
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
    /*planes[0](x,y) = image(x*2+0, y*2+0);
    planes[1](x,y) = image(x*2+1, y*2+0);
    planes[2](x,y) = image(x*2+0, y*2+1);
    planes[3](x,y) = image(x*2+1, y*2+1);*/
    int R = image(x*2+0, y*2+0), G1 = image(x*2+1, y*2+0), G2 = image(x*2+0, y*2+1), B = image(x*2+1, y*2+1);
    planes[0](x,y) = (R+G1+G2+B)/4;
    planes[1](x,y) = (-R+G1+G2-B)/2;
    planes[2](x,y) = (R+B);
    planes[3](x,y) = G2-G1;
   }
   index++;
   log(str(index,3u)+"/"+str(count,3u), name, map.size/1024/1024,"MB","Huffman",cr2.huffmanSize/1024/1024,"MB");
   for(size_t method: range(N)) {
    double entropyCoded = 0;
    Image16 residuals[4];
    for(size_t i: range(4)) {
     const Image16& plane = planes[i];
     Image16& residual = residuals[i];
     residual = Image16(plane.size);
     for(int y: range(plane.size.y)) for(int x: range(plane.size.x)) {
      int left = x>0 ? plane(x-1, y) : 0;
      int top = y>0 ? plane(x, y-1) : 0;
      int topleft = x > 0 && y>0 ? plane(x-1, y-1) : 0;
      int gradient = left+top-topleft;
      int predictor;
      /**/  if(method == 0) predictor = 0;
      else if(method == 1) predictor = left;
      else if(method == 2) predictor = (left+top)/2;
      else if(method == 3) predictor = ::median(left, top, topleft);
      else if(method == 4) predictor = ::median(left, top, gradient);
      else error(method);
      int value = plane(x, y);
      int r = value - predictor;
      assert_(-0x8000 <= r && r <= 0x7FFF);
      residual(x, y) = r;
     }
     int16 min = 0x7FFF, max = 0;
     for(int16 value: residual) { min=::min(min, value); max=::max(max, value); }
     //log(min, max);
     assert_(max+1-min <= 59303, min, max, max+1-min);
     buffer<uint32> histogram(max+1-min);
     histogram.clear(0);
     uint32* base = histogram.begin()-min;
     for(int16 value: residual) base[value]++;
     //uint32 maxCount = 0; for(uint32 count: histogram) maxCount=::max(maxCount, count); log(maxCount);
     const uint32 total = residual.ref::size;
     //log("Uniform", str(total*log2(double(max-min))/8/1024/1024, 0u), "MB");
     for(uint32 count: histogram) if(count) entropyCoded += count * log2(double(total)/double(count));
     //entropyCoded += histogram.size*32*8; //
    }
    //if(method>0) assert_(entropyCoded/8 <= huffmanSize, entropyCoded/8/1024/1024, huffmanSize/1024/1024);
    entropySize[method] += entropyCoded/8;
    if(method>0) log(method, str(entropyCoded/8/1024/1024,0u)+"MB",
                     "Σ", str(entropySize[method]/1024/1024)+"MB =", str((totalSize-entropySize[method])/1024/1024)+"MB ("+str(100*(totalSize-entropySize[method])/totalSize)+"%)");
   }
   break;
  }
  log(decodeTime, totalTime); // 5min
  if(totalSize)
   log(totalSize/1024/1024,"MB -", huffmanSize/1024/1024,"MB =", (totalSize-huffmanSize)/1024/1024,"MB", 100*(totalSize-huffmanSize)/totalSize,"%");
  for(size_t method: range(N)) {
   if(method>0) if(huffmanSize)
    log(method, huffmanSize/1024/1024,"MB -", entropySize[method]/1024/1024,"MB =", (huffmanSize-entropySize[method])/1024/1024,"MB",
        100*(huffmanSize-entropySize[method])/huffmanSize,"%");
   if(method>0) if(totalSize)
    log(method, totalSize/1024/1024,"MB -", entropySize[method]/1024/1024,"MB =", (totalSize-entropySize[method])/1024/1024,"MB",
        100*(totalSize-entropySize[method])/totalSize,"%");
  }
 }
} app;
