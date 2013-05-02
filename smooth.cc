#include "smooth.h"
#include "simd.h"

template<int size> void smooth(uint16* const target, const uint16* const source, int X, int Y, int Z) {
    const uint margin = align(4,size)-size;
    const uint XY = X*Y;
    //#pragma omp parallel for
    for(int z=0; z<Z; z++) for(int x=0; x<X; x+=16) {
        const uint16* const src = source+z*XY+x;
        uint16* const dst = target+x*XY+z*X;
        v8hi sum[2] = {}; //16×16i instructions need AVX2
        for(uint y=margin; y<2*size+margin; y++) for(uint i=0; i<2; i++) sum[i] += loada(src+y*X+i*8);
        for(uint y=margin+size; y<Y-size-margin; y+=8) {
            for(uint i=0; i<2; i++) { // Avoid reloading L1 cache lines twice (L1 = 1024 lines of 16 samples) but transpose one 8x8 tile at a time (SSE = 16 registers of 8 samples)
                v8hi tile[8];
                const uint16* const s = src+y*X+i*8;
                for(int dy=0; dy<8; dy++) {
                    sum[i] += loada(s+(dy+size)*X);
                    tile[dy] = shiftRight(sum[i], log2(2*size+1)); // Rescales intermediate results (limit headroom to npot(N=2*size+1) instead of N³ at the cost of 1bit of precision)
                    sum[i] -= loada(s+(dy-size)*X);
                }
                transpose8(dst+i*8*XY+y, XY, tile[0], tile[1], tile[2], tile[3], tile[4], tile[5], tile[6], tile[7]);
            }
        }
    }
}

/// Denoises a volume using a 3 pass box convolution (max filter size is 2x5+1=15<16)
template<int size> void smooth(Volume16& target, const Volume16& source) {
    uint X = source.x, Y = source.y, Z = source.z;
    // Accumulate along X
    Volume16 tmp(X,Y,Z);
    //FIXME: Shifts source as needed to ensure enough sums will not overflow
    smooth<size>(target, source, X,Y,Z);
    smooth<size>(tmp, target, Y,Z,X);
    smooth<size>(target, tmp, Z,X,Y);
    int den = size*2+1; target.den = source.den * den*den*den;
    int num = 1<<log2(den); target.num = source.num * num*num*num;
    simplify(target.den, target.num);
    assert(target.den/target.num<=(1<<16)); // Whole range can be covered with 16bit
    assert(target.den/target.num>=(1<<12)); // Precision is at least 12bit
    target.marginX += align(4,size), target.marginY += align(4,size), target.marginZ += align(4,size); // Trims volume by filter size
}

template void smooth<2>(Volume16& target, const Volume16& source);
