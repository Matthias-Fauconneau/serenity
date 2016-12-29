#pragma once
#include "bit.h"
static constexpr size_t EG = 0;

struct Decoder : ref<byte> {
    BitReader bitIO;
    int predictor = 0;

    Decoder() {}
    Decoder(ref<byte> data) : ref<byte>(data), bitIO(data) {}

    ref<uint8> read(mref<uint8> buffer) {
        size_t index = 0;
        for(;;) {
            uint u = bitIO.readExpGolomb<EG>();
            int s = (u>>1) ^ (-(u&1)); // u&1 ? -((u>>1) + 1) : u>>1;
            predictor += s;
            buffer[index] = predictor;
            index++;
            if(index>=buffer.size) break;
        }
        return buffer;
    }
};

struct Encoder : buffer<byte> {
    BitWriter bitIO {*this}; // Assumes buffer capacity will be large enough
    int predictor = 0;

    Encoder(size_t capacity) : buffer(capacity) {}
    /*Encoder& write(ref<uint8> source) {
        for(size_t index=0;;) {
            if(index >= source.size) return *this;
            int value = source[index];
            index++;
            int s = value - predictor;
            predictor = value;
            uint u  = (s<<1) ^ (s>>31); // s>0 ? (s<<1) : (-s<<1) - 1;
            // Exp Golomb _ r
            uint x = u + (1<<EG);
            uint b = (sizeof(x)*8-1) - __builtin_clzl(x); // BSR
            bitIO.write(b+b+1-EG, x); // unary b, binary q[b], binary r = 0^b.1.q[b]R[r] = x[b+b+1+r]
        }
        return *this;
    }*/
    void write(int value) {
     int s = value - predictor;
     predictor = value;
     uint u  = (s<<1) ^ (s>>31); // s>0 ? (s<<1) : (-s<<1) - 1;
     // Exp Golomb _ r
     uint x = u + (1<<EG);
     uint b = (sizeof(x)*8-1) - __builtin_clz(x); // BSR
     bitIO.write(b+b+1-EG, x); // unary b, binary q[b], binary r = 0^b.1.q[b]R[r] = x[b+b+1+r]
    }
    ::buffer<byte> end() {
        bitIO.flush();
        size = (byte*)bitIO.pointer - data;
        size += 2*sizeof(word)-1; // No need to pad to avoid page fault with optimized BitReader (whose last refill may stride end) since registers will already pad
        return ::move(*this);
    }
};
