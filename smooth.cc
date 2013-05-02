#include "smooth.h"
#include "simd.h"

/// Returns maximum of data (for debugging)
uint maximum(const Volume16& source) {
    const uint16* const src = source;
    uint64 size = source.size();
    v8hi maximum8 = {};
    for(uint i=0; i<size; i+=8) maximum8 = max(maximum8, loada(src+i));
    uint16 maximum=0; for(uint i: range(8)) maximum = max(maximum, ((uint16*)&maximum8)[i]);
    return maximum;
}

template<uint size, uint shift> void smooth(uint16* const target, const uint16* const source, uint X, uint Y, uint Z) {
    constexpr uint margin = align(4,size)-size;
    const uint XY = X*Y;
    for(uint z=0; z<Z; z++) for(uint x=0; x<X; x+=16) {
        const uint16* const sourceZX = source+z*XY+x;
        uint16* const targetXZ = target+x*XY+z*X;
        v8hi sum[2] = {}; //16×16i instructions would need AVX2
        for(uint y=margin; y<2*size+margin; y++) for(uint i=0; i<2; i++) sum[i] += loada(sourceZX+y*X+i*8);
        for(uint y=margin+size; y<Y-size-margin; y+=8) {
            for(uint i=0; i<2; i++) { // Avoid reloading L1 cache lines twice (L1 = 1024 lines of 16 samples) but transpose one 8x8 tile at a time (SSE = 16 registers of 8 samples)
                v8hi tile[8];
                const uint16* const sourceZYX = sourceZX+y*X+i*8;
                for(uint dy=0; dy<8; dy++) {
                    sum[i] += loada(sourceZYX+int(dy+size)*int(X));
                    tile[dy] = shiftRight(sum[i], shift); // Rescales intermediate results to limit necessary headroom
                    sum[i] -= loada(sourceZYX+int(dy-size)*int(X));
                }
                transpose8(targetXZ+i*8*XY+y, XY, tile[0], tile[1], tile[2], tile[3], tile[4], tile[5], tile[6], tile[7]);
            }
        }
    }
}

void shiftRight(Volume16& target, const Volume16& source, uint shift) {
    const uint16* const src = source;
    uint16* const dst = target;
    uint64 size = source.size();
    for(uint i=0; i<size; i+=8) storea(dst+i, shiftRight(loada(src+i), shift));
}

/// Denoises a volume using a 3 pass box convolution (max filter size is 2x5+1=15<16)
template<int size> void smooth(Volume16& target, const Volume16& original) {
    uint X = original.x, Y = original.y, Z = original.z;
    // Accumulate along X
    Volume16 buffer(X,Y,Z);
    int usedBits = log2(nextPowerOfTwo(original.den/original.num)); // Bits used by data
    constexpr int filterSize = 2*size+1;
    int headBits = log2(nextPowerOfTwo(filterSize)); // Headroom needed to sum without overflow
    const Volume16* source = &original;
    if(usedBits+headBits>16) {
        uint shift = usedBits+headBits-16;
        log("Shifting out",shift,"/",usedBits,"least significant bits to compute sum of",size*2+1,"samples without unpacking to 32bit");
        shiftRight(buffer, original, shift);
        buffer.num = original.num << shift;
        buffer.den = original.den;
        source=&buffer;
    }
    constexpr uint shift = log2(filterSize);
    smooth<size,shift>(target, *source, X,Y,Z);
    smooth<size,shift>(buffer, target, Y,Z,X);
    smooth<size,0>(target, buffer, Z,X,Y);
    target.den = source->den * filterSize*filterSize*filterSize;
    target.num = source->num * (1<<shift)*(1<<shift);
    simplify(target.den, target.num);
    assert(target.den/target.num<(1<<16), target.den, target.num); // Whole range can be covered with 16bit
    assert(target.den/target.num>=(1<<13)-1, target.den, target.num); // Precision is at least 13bit
    target.marginX += align(4,size), target.marginY += align(4,size), target.marginZ += align(4,size); // Trims volume by filter size
}

template void smooth<2>(Volume16& target, const Volume16& source);
