#pragma once

/// Codec for unary/binary encoding
/// \note words are filled lsb first
struct BitReader : array<byte> {
    uint64* pos; //current position
    uint64 w; //current word
    const uint W=64; //word size
    uint bits=0; //used bits in current word
    BitReader(array<byte>&& buffer) : array(move(buffer)), pos((uint64*)data), w(*pos++) {}

    /// Reads an unary encoded value
    inline uint unary() {
        uint v=0;
        if(!w) w=*pos++, v=W-bits, bits=0;
        assert(w);
        uint ffs =__builtin_ffsl(w);
        assert(ffs,str(w,2,64));
        v+=ffs-1; w>>=ffs; bits+=ffs;
        return v;
    }

    /// Reads \a size bits in LSB lsb encoding
    inline uint binary(uint size) {
        uint v = w;
        bits += size;
        if(bits<W) w >>= size, v &= ((1ul<<size)-1);
        else bits -= W, w=*pos++, v |= (w & ((1ul<<bits)-1)) << (size-bits), w>>=bits;
        return v;
    }
};

struct Codec : BitReader {
    static const uint N=8;
    int last[2]={0,0}, previous[2]={0,0}; uint mean[2]={1<<8,1<<8};
    Codec(array<byte>&& buffer) : BitReader(move(buffer)) {}
    int operator()(int c) {
        uint m = mean[c];
        uint k = m ? 32-__builtin_clz(m) : 1;
        uint u = unary() << k;
        u |= binary(k);
        mean[c] = ((N-1)*m+u)/N;
        int e = (u >> 1) ^ (-(u & 1));
        int s = (2*last[c]-previous[c]) + e;
        previous[c] = last[c];
        last[c] = s;
        return s;
    }
};
