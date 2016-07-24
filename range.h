#pragma once
/// \file range.h Carry-less range coder (Dmitry Subbotin, Sachin Garg (http://www.sachingarg.com/compression/entropy_coding/64bit))
#include "core.h"

struct RangeCoder{
    static constexpr uint64 bottom = 1ull<<48, maxRange = bottom, top=1ull<<56;
    uint64 low = 0, range = -1;
};

struct RangeEncoder : RangeCoder {
    byte* output;
    void operator()(uint symbolLow, uint symbolHigh, uint totalRange) {
        low += symbolLow*(range/=totalRange);
        range *= symbolHigh-symbolLow;
        while((low ^ (low+range))<top || range<bottom && ((range= -low & (bottom-1)),1)) {
            *output = low>>56; output++;
            range <<= 8;
            low <<= 8;
        }
    }
    ~RangeEncoder() {
        for(uint unused i: ::range(8)) {
            *output = low>>56; output++;
            low<<=8;
        }
    }
};

struct RangeDecoder: RangeCoder {
    const byte* input;
    uint64 code = 0;

    RangeDecoder(const byte* const input_) : input(input_) {
        for(uint unused i: ::range(8)) { code = (code << 8) | *input; input++; }
    }
    uint32 getCurrentCount(uint32 totalRange) { return (code-low)/(range/=totalRange); }
    void removeRange(uint32 symbolLow, uint32 symbolHigh) {
        low += symbolLow*range;
        range *= symbolHigh-symbolLow;
        while((low ^ low+range)<top || range<bottom && ((range= -low & bottom-1),1)) {
            code = code<<8 | *input; input++;
            range<<=8;
            low<<=8;
        }
    }
};

