#include "cr2.h"
#include "time.h"
#include <smmintrin.h>
inline double log2(double x) { return __builtin_log2(x); }

struct Raw {
 Raw() {
  size_t imageCount = 0;
  array<String> files = Folder(".").list(Files|Sorted);
  for(string name: files) {
   if(!endsWith(toLower(name), ".cr2")) continue;
   String jpgName = section(name,'.')+".JPG";
   if(files.contains(jpgName)) {
    assert_(existsFile(section(name,'.')+".CR2"));
    log("Removing", jpgName);
    remove(jpgName);
    //return;
   }
   //assert_(!files.contains(section(name,'.')+".JPG"), name);
   Map file(name);
   CR2 cr2(file, true);
   const size_t source = cr2.data.begin()-file.begin();
   const size_t target = 0x3800;
   if(cr2.ifdOffset.size <= 3 && source == target) continue;
   imageCount++;
  }
  Time readTime, encodeTime, decodeTime, totalTime {true};
  size_t totalSize = 0, stripSize = 0, archiveSize = 0;
  size_t index = 0;
  for(string name: files) {
   if(!endsWith(toLower(name), ".cr2")) continue;
   if(0) {
    Map file(name);
    CR2 cr2(file, true);
    const size_t source = cr2.data.begin()-file.begin();
    const size_t target = 0x3800;
    if(cr2.ifdOffset.size <= 3 && source == target) continue;
   }
   index++;
   log(index,"/",imageCount,name);
   if(0) {
    buffer<byte> file = readFile(name);
    {
     CR2 cr2(file, true);
     const size_t source = cr2.data.begin()-file.begin();
     const size_t target = 0x3800;
     assert_(cr2.ifdOffset.size == 5 && source > target);
     for(CR2::Entry* entry: cr2.entriesToFix) {
      if(entry->tag==0x102) { entry->count=1; entry->value=16; } // BitsPerSample
      else if(entry->tag == 0x111) { entry->value = target; } // StripOffset
      else if(entry->tag == 0x2BC) { entry->count = 0; entry->value = 0; } // XMP
      else error(entry->tag);
     }
     *cr2.ifdOffset[1] = *cr2.ifdOffset[3]; // Skips JPEG and RGB thumbs
     for(size_t i: range(cr2.data.size)) file[target+i] = file[source+i];
     size_t originalSize = file.size;
     file.size -= source-target;
     log(str(originalSize/1024/1024., 2u), "MB", str((source-target)/1024/1024., 2u), "MB", str(100.*(source-target)/file.size, 2u)+"%",
           str(file.size/1024/1024., 2u), "MB");
     stripSize += file.size;
    }
    {CR2 cr2(file);} // Verify
    writeFile(name, file, currentWorkingDirectory(), true);
   }
   if(1) {
    Map file(name);
    totalSize += file.size;
    readTime.start();
    CR2 cr2(file);
    readTime.stop();

    const Image16& image = cr2.image;
    // FIXME: compute residual images directly from RGGB layout image
    Image16 planes[4] = {}; // R, G1, G2, B
    for(size_t i: range(4)) planes[i] = Image16(image.size/2);
    for(size_t y: range(image.size.y/2)) for(size_t x: range(image.size.x/2)) {
     planes[0](x,y) = image(x*2+0, y*2+0);
     planes[1](x,y) = image(x*2+1, y*2+0);
     planes[2](x,y) = image(x*2+0, y*2+1);
     planes[3](x,y) = image(x*2+1, y*2+1);
    }
    size_t compressedSize = 0;
    for(size_t i: range(4)) {

     const Image16& plane = planes[i];
     Image16 residual (plane.size);
     buffer<uint16> buffer (residual.ref::size);

     encodeTime.start();

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
     ::buffer<uint32> histogram(max+1-min);
     histogram.clear(0);
     uint32* base = histogram.begin()-min;
     for(int value: residual) base[value]++;
     ::buffer<uint> cumulative(1+histogram.size);
     cumulative[0] = 0;
     for(size_t i: range(histogram.size)) cumulative[i+1] = cumulative[i] + histogram[i];

     static constexpr uint L = 1u << 16;
     static constexpr uint scaleBits = 15; // < 16
     static constexpr uint M = 1<<scaleBits;

     ::buffer<uint16> freqM (histogram.size);
     ::buffer<uint16> cumulativeM (cumulative.size);
     cumulativeM[0] = 0;
     for(size_t i: range(1, cumulative.size)) cumulativeM[i] = (uint64)cumulative[i]*M/cumulative.last();
     for(size_t i: range(1, cumulative.size)) {
      if(histogram[i-1] && cumulativeM[i] == cumulativeM[i-1]) {
       uint32 bestFreq = 0;
       size_t bestIndex = invalid;
       for(int j: range(1, cumulative.size)) {
        uint32 freq = cumulativeM[j] - cumulativeM[j-1];
        if(freq > 1 && freq > bestFreq) { bestFreq = freq; bestIndex = j; }
       }
       assert_(bestIndex != invalid && bestIndex != i);
       if(bestIndex < i) for(size_t j: range(bestIndex, i)) cumulativeM[j]--;
       if(bestIndex > i) for(size_t j: range(i, bestIndex)) cumulativeM[j]++;
      }
     }
     assert_(cumulativeM[0] == 0 && cumulativeM.last() == M, cumulativeM.last());
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
     double entropy = 0;
     for(uint32 count: histogram) if(count) entropy += count * log2(double(total)/double(count));
     log(str(100.*(bufferSize/(entropy/16)-1), 1u)+"%", str(100.*histogram.size/bufferSize, 1u)+"%");
     compressedSize += (histogram.size+bufferSize)*2;

     decodeTime.start();

     uint16 reverse[M];
     uint slots[M];
     for(uint16 sym: range(histogram.size)) {
      for(uint16 i: range(freqM[sym])) {
       uint slot = cumulativeM[sym]+i;
       reverse[slot] = min+sym;
       slots[slot] = uint(freqM[sym]) | (uint(i) << 16); // uint16 freq, bias;
      }
     }

     uint16* ptr = begin;
     __v4si x = _mm_loadu_si128((const __m128i*)ptr);
     ptr += 8; // !! not 16
     Image16 target (residual.size);
     assert_(target.ref::size%4 == 0);
     for(size_t i=0; i<target.ref::size; i += 4) {
      const __v4si slot = _mm_and_si128(x, _mm_set1_epi32(M - 1));
      target[i+0] = reverse[slot[0]];
      target[i+1] = reverse[slot[1]];
      target[i+2] = reverse[slot[2]];
      target[i+3] = reverse[slot[3]];

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

     decodeTime.stop();

     for(size_t i: range(target.ref::size)) assert_(target[i] == residual[i]);
    }
    archiveSize += compressedSize;
    log(str(compressedSize/1024/1024,0u)+"MB",
        "Σ", str(archiveSize/1024/1024)+"MB =", str((totalSize-archiveSize)/1024/1024)+"MB ("+str(100*(totalSize-archiveSize)/totalSize)+"%)");
    break;
   }
  }
  log(readTime, encodeTime, decodeTime, totalTime); // 5min
  if(totalSize) log(str(totalSize/1024/1024., 2u),"MB -", str(archiveSize/1024/1024.,2u),"MB",str(100.*(totalSize-archiveSize)/totalSize, 1u),"% =",
                           str((totalSize-archiveSize)/1024/1024., 2u),"MB");
 }
} app;
