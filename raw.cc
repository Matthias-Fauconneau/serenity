#include "cr2.h"
#include "time.h"
#include "range.h"
inline double log2(double x) { return __builtin_log2(x); }

/*void rescale(const mref<uint16> target, const ref<uint32> source) {
 target[0] = 0;
 for(int i: range(1, source.size)) {
  uint t = source[i]*65536/source.last();
  //if(!(target[i-1]<t)) t = target[i-1]+1;
  assert_(t < 65536 && target[i-1] <= t, source[i-1], source[i]);
  target[i] = t;
 }
}*/

struct Raw {
 Raw() {
  size_t imageCount = 0;
  for(string name: Folder(".").list(Files|Sorted)) if(endsWith(toLower(name), ".cr2")) imageCount++;
  Time decodeTime, encodeTime, verifyTime, totalTime {true};
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
    for(int value: residual) base[value]++;
    buffer<uint> cumulative(1+histogram.size);
    cumulative[0] = 0;
    for(size_t i: range(histogram.size)) cumulative[i+1] = cumulative[i] + histogram[i];
    buffer<uint16> cumulative16 (cumulative.size); // Quantize entropy to reduce decoder lookup table
    //rescale(cumulative16, cumulative);
    cumulative16[0] = 0;
    for(size_t i: range(1, cumulative.size)) {
     uint t = cumulative[i]*64512ull/cumulative.last(); // not 1<<16 as headroom is required to ceil last symbols
     if(!(cumulative16[i-1]<t)) {
      if(cumulative[i-1] == cumulative[i]) t = cumulative16[i-1];
      else if(cumulative[i-1] < cumulative[i]) t = cumulative16[i-1]+1;
      else error("");
     }
     assert_((t < 65536 || (i==cumulative.size-1 && t==65536)) && (cumulative16[i-1] < t || (cumulative16[i-1]==t && cumulative[i-1]==cumulative[i])),
       cumulative[i-1], cumulative[i], cumulative16[i-1], t, i, cumulative.size);
     cumulative16[i] = t;
    }
    const uint totalRange = cumulative16.last();
    assert_(uint64(totalRange) < RangeCoder::maxRange);
    buffer<byte> buffer (residual.ref::size); // ~ 5bps
    encodeTime.start();
    RangeEncoder encode (buffer);
    for(int r: residual) {
     uint s = r-min; // Symbol
     //assert(cumulative16[s]<cumulative16[s+1]);
     encode(cumulative16[s], cumulative16[s+1], totalRange);
    }
    encode.flush();
    buffer.size = (byte*)encode.output - buffer.begin();
    encodeTime.stop();

    const uint32 total = residual.ref::size;
    double planeEntropyCoded = 0;
    for(uint32 count: histogram) if(count) planeEntropyCoded += count * log2(double(total)/double(count));
    log(str(100*histogram.size*32/planeEntropyCoded)+"%");
    //planeEntropyCoded += histogram.size * 32; // 10K
    entropyCoded += planeEntropyCoded;
    log(buffer.size, uint(planeEntropyCoded/8), buffer.size/(planeEntropyCoded/8));

    // Verify
    verifyTime.start();
    RangeDecoder decode (buffer.begin());
    log(totalRange);
    ::buffer<int16> reverse (totalRange); // FIXME: scale range to reduce lookup table size
    for(int symbol: range(cumulative16.size-1)) {
     assert_(cumulative16[symbol] < reverse.size);
     for(int k: range(cumulative16[symbol], cumulative16[symbol+1])) reverse[k] = symbol;
    }
    for(int r: residual) {
     uint reference = r-min;
     uint key = decode(totalRange);
#if 0 // 30s
     uint s; for(s = max-min; key < cumulative16[s]; s--) {}
#elif 0 // 3.6s
     /// Returns index to the last element lesser than \a key using binary search (assuming a sorted array)
     uint min=0, max=cumulative16.size-1;
     do {
         size_t mid = (min+max+1)/2;
         if(cumulative16[mid] <= key) min = mid;
         else max = mid-1;
     } while(min<max);
     uint s = min;
#else // 1.7s
     assert_(key < reverse.size, key, reverse.size);
     uint s = reverse[key];
#endif
     assert_(reference == s, reference, s, key);
     decode.next(cumulative16[s], cumulative16[s+1]);
    }
   }
   verifyTime.stop();
   //assert_(entropyCoded/8 <= huffmanSize, entropyCoded/8/1024/1024, huffmanSize/1024/1024);
   entropySize += entropyCoded/8;
   log(str(entropyCoded/8/1024/1024,0u)+"MB",
       "Σ", str(entropySize/1024/1024)+"MB =", str((totalSize-entropySize)/1024/1024)+"MB ("+str(100*(totalSize-entropySize)/totalSize)+"%)");
   break;
  }
  log(decodeTime, encodeTime, verifyTime, totalTime); // 5min
  if(totalSize) log(totalSize/1024/1024,"MB -", huffmanSize/1024/1024,"MB =", (totalSize-huffmanSize)/1024/1024,"MB", 100*(totalSize-huffmanSize)/totalSize,"%");
  if(huffmanSize)
   log(huffmanSize/1024/1024,"MB -", entropySize/1024/1024,"MB =", (huffmanSize-entropySize)/1024/1024,"MB", 100*(huffmanSize-entropySize)/huffmanSize,"%");
  if(totalSize) log(totalSize/1024/1024,"MB -", entropySize/1024/1024,"MB =", (totalSize-entropySize)/1024/1024,"MB", 100*(totalSize-entropySize)/totalSize,"%");
 }
} app;
