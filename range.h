#pragma once
/// \file range.h Carry-less range coder (Dmitry Subbotin, Sachin Garg (http://www.sachingarg.com/compression/entropy_coding/64bit))
#include "core.h"

struct RangeCoder{
 static constexpr uint64 bottom = 1ull<<48, maxRange = bottom, top=1ull<<56;
 uint64 low = 0, range = -1;
};

struct RangeEncoder : RangeCoder {
 mref<byte> buffer;
 uint8* output;
 RangeEncoder(mref<byte> buffer) : buffer(buffer), output((uint8*)buffer.begin()) {}
 void operator()(uint64 symbolLow, uint64 symbolHigh, uint64 totalRange) {
  low += symbolLow*(range/=totalRange);
  range *= symbolHigh-symbolLow;
  while( (low^(low+range))<top || range<bottom && ((range=-low&(bottom-1)),1)) {
   *output = low>>56; output++;
   range <<= 8;
   low <<= 8;
  }
 }
 void flush() {
  for(uint unused i: ::range(8)) {
   *output = low>>56; output++;
   low <<= 8;
  }
 }
};

struct RangeDecoder: RangeCoder {
 const uint8* input;
 uint64 code = 0;

 RangeDecoder(const byte* const input_) : input((const uint8*)input_) {
  for(uint unused i: ::range(8)) { code = (code << 8) | *input; input++; }
 }
 uint64 operator()(uint64 totalRange) {
  return (code-low)/(range/=totalRange);
 }
 void next(uint64 symbolLow, uint64 symbolHigh) {
  low += symbolLow*range;
  range *= symbolHigh-symbolLow;
  while((low^(low+range))<top || range<bottom && ((range = -low&(bottom-1)),1)) {
   code = code<<8 | *input; input++;
   range <<= 8;
   low <<= 8;
  }
 }
};
