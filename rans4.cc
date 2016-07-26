#include "rans4.h"

static constexpr uint L = 1u << 16;
static constexpr uint scaleBits = 15; // < 16
static constexpr uint M = 1<<scaleBits;

typedef float m128 __attribute__((__vector_size__(16)));
typedef int __attribute((__vector_size__(16))) v4si;
typedef char v16qi __attribute__((__vector_size__(16)));
inline v4si set1(int i) { return (v4si){i,i,i,i}; }
#include <smmintrin.h>

void decodeRANS4(const Image16& target, const ref<byte> source) {
 const uint16* pointer = (uint16*)source.begin();
 for(size_t i: range(4)) {
  int16 min = *pointer; pointer++;
  int16 max = *pointer; pointer++;
  ref<uint16> freqM (pointer, max+1-min);
  pointer += freqM.size;
  ::buffer<uint16> cumulativeM(1+freqM.size);
  cumulativeM[0] = 0;
  for(size_t i: range(freqM.size)) cumulativeM[i+1] = cumulativeM[i] + freqM[i];

  int16 reverse[M]; // 64K
  uint slots[M]; // 128K
  for(uint16 sym: range(freqM.size)) {
   for(uint16 i: range(freqM[sym])) {
    uint slot = cumulativeM[sym]+i;
    reverse[slot] = min+sym;
    slots[slot] = uint(freqM[sym]) | (uint(i) << 16); // uint16 freq, bias;
   }
  }

  v4si x = (v4si)_mm_loadu_si128((const __m128i*)pointer);
  pointer += 8; // !! not 16
  uint W = target.width/2;
  assert_(W%4 == 0);
  uint16* plane = target.begin() + (i&2)*W + (i&1);
  for(uint y: range(target.height/2)) {
   const uint16* const up = plane + (y-1)*2*W*2;
   uint16* const row = plane + y*2*W*2;
   for(uint X=0; X<W; X+=4) {
    const v4si slot = x & set1(M-1);
    for(uint k: range(4)) { // FIXME: SIMD
     uint x = X+k;
     uint top = y>0 ? up[x*2] : 0;
     uint left = x>0 ? row[x*2-2] : 0;
     int predictor = (left+top)/2;
     int r = reverse[slot[k]];
     uint value = predictor + r;
     row[x*2] = value;
    }

    const v4si freq_bias {(int)slots[slot[0]], (int)slots[slot[1]], (int)slots[slot[2]], (int)slots[slot[3]]};
    const v4si xscaled = __builtin_ia32_psrldi128(x, scaleBits);
    v4si freq = freq_bias & set1(0xffff);
    v4si bias = __builtin_ia32_psrldi128(freq_bias, 16);
    x = xscaled * freq + bias;

    static v16qi const shuffles[16] = {
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
    v4si x_biased = x ^ set1(0x80000000);
    v4si greater = set1(L - 0x80000000) > x_biased;
    uint mask = __builtin_ia32_movmskps((m128)greater);
    v4si xshifted = __builtin_ia32_pslldi128(x, 16);
    v4si newx = xshifted | (v4si)__builtin_ia32_pshufb128((v16qi)_mm_loadl_epi64((const __m128i*)pointer), shuffles[mask]);
    x = (v4si)__builtin_ia32_pblendvb128((v16qi)x, (v16qi)newx, (v16qi)greater);
    pointer += numBytes[mask];
   }
  }
 }
}

size_t encodeRANS4(const mref<byte> target, const Image16& source) {
 struct Buffer : mref<uint16> {
  size_t size = 0;
  Buffer(const mref<uint16>& buffer) : mref<uint16>(buffer) {}
  void append(int16 e) { at(size++) = e; }
  void append(const ref<uint16> source) { slice(size, source.size).copy(source); size += source.size; }
 } buffer (mcast<uint16>(target));
 assert_(source.width%2==0 && source.height%2==0);
 Image16 residual (source.size/2);
 for(uint planeIndex: range(4)) {
  uint W = source.width/2;
  assert_(W%4 == 0);
  const uint16* plane = source.data + (planeIndex&2)*W + (planeIndex&1);
  for(uint y: range(source.height/2)) {
   const uint16* const up = plane + (y-1)*2*W*2;
   const uint16* const row = plane + y*2*W*2;
   uint16* const target = residual.begin() + y*W;
   for(uint x: range(W)) {
    uint top = y>0 ? up[x*2] : 0;
    uint left = x>0 ? row[x*2-2] : 0;
    int predictor = (left+top)/2;
    uint value = row[x*2];
    int r = value - predictor;
    assert_(-0x2000 <= r && r <= 0x2000, r, hex(r));
    target[x] = r;
   }
  }

  int16 min = 0x7FFF, max = 0;
  for(int16 value: residual) { min=::min(min, value); max=::max(max, value); }
  assert_(max+1-min <= 0x2000);
  ::buffer<uint32> histogram(max+1-min);
  histogram.clear(0);
  uint32* base = histogram.begin()-min;
  for(int value: residual) base[value]++;
  ::buffer<uint> cumulative(1+histogram.size);
  cumulative[0] = 0;
  for(size_t i: range(histogram.size)) cumulative[i+1] = cumulative[i] + histogram[i];

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

  uint16* const end = buffer.end();
  uint16* begin; {
   uint16* pointer = end;
   uint32 rans[4];
   for(uint32& r: rans) r = L;
   for(int i: reverse_range(residual.ref::size)) {
    uint s = residual[i]-min;
    uint32 x = rans[i%4];
    uint freq = freqM[s];
    if(x >= ((L>>scaleBits)<<16) * freq) {
     pointer -= 1;
     *pointer = (uint16)(x&0xffff);
     x >>= 16;
    }
    rans[i%4] = ((x / freq) << scaleBits) + (x % freq) + cumulativeM[s];
   }
   for(int i: reverse_range(4)) {
    uint32 x = rans[i%4];
    pointer -= 2;
    pointer[0] = (uint16)(x>>0);
    pointer[1] = (uint16)(x>>16);
   }
   assert_(pointer >= buffer.begin());
   begin = pointer;
  }
  buffer.append(min);
  buffer.append(max);
  buffer.append(freqM);
  assert_(begin > buffer.end());
  buffer.append(ref<uint16>(begin, end-begin));
 }
 return buffer.size*sizeof(uint16);
}
