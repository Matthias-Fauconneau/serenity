#include "cr2.h"
#include "time.h"
#include <smmintrin.h>
inline double log2(double x) { return __builtin_log2(x); }

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

    buffer<uint16> buffer (residual.ref::size); // ~ 5bps
    encodeTime.start();

    static constexpr uint L = 1u << 16;
    static constexpr uint scaleBits = 12; // <= 16
    static constexpr uint M = 1<<scaleBits;

    ::buffer<uint16> freqM (histogram.size);
    ::buffer<uint16> cumulativeM (cumulative.size);
    cumulativeM[0] = 0;
    for(size_t i: range(1, cumulative.size)) {
     cumulativeM[i] = (uint64)cumulative[i]*M/cumulative.last();
    }
    for(size_t i: range(1, cumulative.size)) {
     if(histogram[i-1] && cumulativeM[i] == cumulativeM[i-1]) {
      uint32 bestFreq = 0; //~0u;
      size_t bestIndex = invalid;
      for(int j: range(1, cumulative.size)) {
       uint32 freq = cumulativeM[j] - cumulativeM[j-1];
       if(freq > 1 && freq >/*<*/ bestFreq) { bestFreq = freq; bestIndex = j; }
      }
      assert_(bestIndex != invalid && bestIndex != i);
      if(bestIndex < i) for(size_t j: range(bestIndex, i)) cumulativeM[j]--;
      if(bestIndex > i) for(size_t j: range(i, bestIndex)) cumulativeM[j]++;
     }
    }
    assert_(cumulativeM[0] == 0 && cumulativeM.last() == M);
    for(size_t i: range(histogram.size)) {
     if(histogram[i] == 0) assert_(cumulativeM[i+1] == cumulativeM[i]);
     else assert_(cumulativeM[i+1] > cumulativeM[i]);
     freqM[i] = cumulativeM[i+1] - cumulativeM[i];
    }

    uint16* begin; {
     uint16* ptr = buffer.end();
     uint32 rans[4];
     for(uint32& r: rans) r = L;
     for(int i: reverse_range(residual.ref::size)) {
      uint s = residual[i]-min;
      uint32 x = rans[i%4];
      uint freq = freqM[s];
      if(x >= ((L>>scaleBits)<<16) * freq) {
       ptr -= 1;
       *ptr = (uint16)(x&0xffff);
       x >>= 16;
      }
      rans[i%4] = ((x / freq) << scaleBits) + (x % freq) + cumulativeM[s];
     }
     for(int i: reverse_range(4)) {
      uint32 x = rans[i%4];
      ptr -= 2;
      ptr[0] = (uint16)(x>>0);
      ptr[1] = (uint16)(x>>16);
     }
     assert_(ptr >= buffer.begin());
     begin = ptr;
    }
    size_t bufferSize = buffer.end()-begin;
    encodeTime.stop();

    const uint32 total = residual.ref::size;
    double planeEntropyCoded = 0;
    for(uint32 count: histogram) if(count) planeEntropyCoded += count * log2(double(total)/double(count));
    log(str(100*histogram.size*32/planeEntropyCoded)+"%");
    //planeEntropyCoded += histogram.size * 32; // 10K
    entropyCoded += planeEntropyCoded;
    log(bufferSize*2, uint(planeEntropyCoded/8), bufferSize/(planeEntropyCoded/16));

    // Verify
    verifyTime.start();

    uint16 reverse[M];
    uint slots[M];
    for(uint16 sym: range(histogram.size)) {
     for(uint16 i: range(freqM[sym])) {
      uint slot = cumulativeM[sym]+i;
      reverse[slot] = sym;
      // uint16 freq, bias;
      slots[slot] = uint(freqM[sym]) | (uint(i) << 16);
     }
    }

    uint16* ptr = begin;
    __v4si x = _mm_loadu_si128((const __m128i*)ptr);
    ptr += 8; // !! not 16
    assert_(residual.ref::size%4 == 0);
    for(size_t i=0; i<residual.ref::size; i += 4) {
     __v4si slot = _mm_and_si128(x, _mm_set1_epi32(M - 1));
     for(int k: range(4)) assert_(residual[i+k]-min == reverse[slot[k]]);

     __v4si freq_bias_lo = _mm_cvtsi32_si128(slots[slot[0]]);
     freq_bias_lo = _mm_insert_epi32(freq_bias_lo, slots[slot[1]], 1);
     __v4si freq_bias_hi = _mm_cvtsi32_si128(slots[slot[2]]);
     freq_bias_hi = _mm_insert_epi32(freq_bias_hi, slots[slot[3]], 1);
     __v4si freq_bias = _mm_unpacklo_epi64(freq_bias_lo, freq_bias_hi);
     __v4si xscaled = _mm_srli_epi32(x, scaleBits);
     __v4si freq = _mm_and_si128(freq_bias, _mm_set1_epi32(0xffff));
     __v4si bias = _mm_srli_epi32(freq_bias, 16);
     x = xscaled * freq + bias;

     static int8 const shuffles[16][16] = {
 #define _ -1
      { _,_,_,_, _,_,_,_, _,_,_,_, _,_,_,_ }, // 0000
      { 0,1,_,_, _,_,_,_, _,_,_,_, _,_,_,_ }, // 0001
      { _,_,_,_, 0,1,_,_, _,_,_,_, _,_,_,_ }, // 0010
      { 0,1,_,_, 2,3,_,_, _,_,_,_, _,_,_,_ }, // 0011
      { _,_,_,_, _,_,_,_, 0,1,_,_, _,_,_,_ }, // 0100
      { 0,1,_,_, _,_,_,_, 2,3,_,_, _,_,_,_ }, // 0101
      { _,_,_,_, 0,1,_,_, 2,3,_,_, _,_,_,_ }, // 0110
      { 0,1,_,_, 2,3,_,_, 4,5,_,_, _,_,_,_ }, // 0111
      { _,_,_,_, _,_,_,_, _,_,_,_, 0,1,_,_ }, // 1000
      { 0,1,_,_, _,_,_,_, _,_,_,_, 2,3,_,_ }, // 1001
      { _,_,_,_, 0,1,_,_, _,_,_,_, 2,3,_,_ }, // 1010
      { 0,1,_,_, 2,3,_,_, _,_,_,_, 4,5,_,_ }, // 1011
      { _,_,_,_, _,_,_,_, 0,1,_,_, 2,3,_,_ }, // 1100
      { 0,1,_,_, _,_,_,_, 2,3,_,_, 4,5,_,_ }, // 1101
      { _,_,_,_, 0,1,_,_, 2,3,_,_, 4,5,_,_ }, // 1110
      { 0,1,_,_, 2,3,_,_, 4,5,_,_, 6,7,_,_ }, // 1111
 #undef _
     };
     static uint8 const numBytes[16] = { 0,1,1,2, 1,2,2,3, 1,2,2,3, 2,3,3,4 };
     __v4si x_biased = _mm_xor_si128(x, _mm_set1_epi32(int(0x80000000)));
     __v4si greater = _mm_cmplt_epi32(x_biased, _mm_set1_epi32(L - 0x80000000));
     uint mask = _mm_movemask_ps(greater);
     __v4si memvals = _mm_loadl_epi64((const __m128i*)ptr);
     __v4si xshifted = _mm_slli_epi32(x, 16);
     __v4si shufmask = _mm_load_si128((const __m128i*)shuffles[mask]);
       __v4si newx = _mm_or_si128(xshifted, _mm_shuffle_epi8(memvals, shufmask));
     x = _mm_blendv_epi8(x, newx, greater);
     ptr += numBytes[mask];
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
