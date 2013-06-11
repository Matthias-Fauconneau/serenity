#include "volume-operation.h"
#include "thread.h"
#include "simd.h"

/// Shifts all values to the right
void shiftRight(Volume16& target, const Volume16& source, uint shift) {
    const uint16* const src = source;
    uint16* const dst = target;
    uint64 size = source.size();
    assert_(size%8==0);
    for(uint i=0; i<size; i+=8) storea(dst+i, shiftRight(loada(src+i), shift));
}

/// Shifts data before summing to avoid overflow
class(ShiftRight, Operation), virtual VolumePass<uint16> {
    ref<byte> parameters() const override { return "smoothKernelSize"_; }
    void execute(const Dict& args, Volume16& target, const Volume& source) override {
        int kernelSize = toInteger(args.at("smoothKernelSize"_)), sampleCount = 2*kernelSize+1, shift = log2(sampleCount);
        int max = ((((target.maximum*sampleCount)>>shift)*sampleCount)>>shift)*sampleCount;
        int bits = log2(nextPowerOfTwo(max));
        int headroomShift = ::max(0,bits-16);
        shiftRight(target, source, headroomShift);
        target.maximum >>= headroomShift;
    }
};

/// Computes one pass of running average
void smooth(Volume16& target, const Volume16& source, uint size, uint shift) {
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    const uint sX=source.sampleCount.x, sY=source.sampleCount.y, sZ=source.sampleCount.z;
    const uint tX=sY, tY=sZ;
    const uint margin = align(4,size)-size;
    assert_(sX%16==0 && (tX-size-margin-margin-size)%8==0);
    parallel(sZ, [&](uint, uint z){
        for(uint x=0; x<sX; x+=16) {
            const uint16* const sourceZX = sourceData + z*sX*sY + x;
            v8hi sum[2] = {}; //16×16i instructions would need AVX2
            for(uint y=margin; y<2*size+margin; y++) for(uint i=0; i<2; i++) sum[i] += loada(sourceZX+y*sX+i*8);
            for(uint y=margin+size; y<sY-size-margin; y+=8) {
                for(uint i=0; i<2; i++) { // Avoid reloading L1 cache lines twice (L1 = 1024 lines of 16 samples) but transpose one 8x8 tile at a time (SSE = 16 registers of 8 samples)
                    v8hi tile[8];
                    const uint16* const sourceZYX = sourceZX+y*sX+i*8;
                    for(uint dy=0; dy<8; dy++) {
                        sum[i] += loada(sourceZYX+int(dy+size)*int(sX));
                        tile[dy] = shiftRight(sum[i], shift); // Rescales intermediate results to limit necessary headroom
                        sum[i] -= loada(sourceZYX+int(dy-size)*int(sX));
                    }
                    transpose8(targetData + (x+i*8)*tX*tY + z*tX + y, tX*tY, tile[0], tile[1], tile[2], tile[3], tile[4], tile[5], tile[6], tile[7]);
                }
            }
        }
    } );
}

/// Denoises data by averaging samples in a window
class(Smooth, Operation), virtual VolumePass<uint16> {
    ref<byte> parameters() const override { return "smoothKernelSize shift"_; }
    void execute(const Dict& args, Volume16& target, const Volume& source) override {
        int kernelSize = toInteger(args.at("smoothKernelSize"_)), sampleCount = 2*kernelSize+1, shift = args.value("shift"_,log2(sampleCount));
        target.margin.y += align(4, kernelSize);
        target.maximum *= sampleCount; target.maximum >>= shift;
        target.sampleCount=rotate(target.sampleCount); target.margin=rotate(target.margin);
        smooth(target, source, kernelSize, shift);
    }
};
