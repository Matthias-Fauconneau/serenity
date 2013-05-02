#include "volume.h"
#include "simd.h"

static buffer<uint> interleavedLookup(uint size, uint offset, uint stride=3) {
    buffer<uint> lookup(size);
    for(uint i=0; i<size; i++) { lookup[i]=0; for(uint b=0, bits=i; bits!=0; bits>>=1, b++) { uint bit=bits&1; lookup[i] |= bit << (b*stride+offset); } }
    return lookup;
}

void interleavedLookup(Volume& target) {
    if(!target.offsetX) target.offsetX = interleavedLookup(target.x,0);
    if(!target.offsetY) target.offsetY = interleavedLookup(target.y,1);
    if(!target.offsetZ) target.offsetZ = interleavedLookup(target.z,2);
}

/// Returns maximum of data (for debugging)
uint maximum(const Volume16& source) {
    const uint16* const src = source;
    uint64 size = source.size();
    v8hi maximum8 = {};
    for(uint i=0; i<size; i+=8) maximum8 = max(maximum8, loada(src+i));
    uint16 maximum=0; for(uint i: range(8)) maximum = max(maximum, ((uint16*)&maximum8)[i]);
    return maximum;
}

/// Returns maximum of data (for debugging)
uint maximum(const Volume32& source) {
    const uint32* const src = source;
    uint64 size = source.size();
    v4si maximum4 = {};
    for(uint i=0; i<size; i+=8) maximum4 = max(maximum4, loada(src+i));
    uint32 maximum=0; for(uint i: range(4)) maximum = max(maximum, ((uint32*)&maximum4)[i]);
    return maximum;
}

/// Downsamples a volume by averaging 2x2x2 samples
void downsample(Volume16& target, const Volume16& source) {
    int X = source.x, Y = source.y, Z = source.z, XY = X*Y;
    target.x = X/2, target.y = Y/2, target.z = Z/2; target.num=source.num, target.den=source.den;
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    for(int z=0; z<Z/2; z++) {
        const uint16* const sourceZ = sourceData+z*2*XY;
        uint16* const targetZ = targetData+z*XY/2/2;
        for(int y=0; y<Y/2; y++) {
            const uint16* const sourceZY = sourceZ+y*2*X;
            uint16* const targetZY = targetZ+y*X/2;
            for(int x=0; x<X/2; x++) {
                const uint16* const sourceZYX = sourceZY+x*2;
                targetZY[x] =
                        (
                            ( sourceZYX[0*XY+0*X+0] + sourceZYX[0*XY+0*X+1] +
                        sourceZYX[0*XY+1*X+0] + sourceZYX[0*XY+1*X+1]  )
                        +
                        ( sourceZYX[1*XY+0*X+0] + sourceZYX[1*XY+0*X+1] +
                        sourceZYX[1*XY+1*X+0] + sourceZYX[1*XY+1*X+1]  ) ) / 8;
            }
        }
    }
}

/// Returns an image of a volume slice
Image slice(const Volume& volume, uint z) {
    uint X=volume.x, Y=volume.y;
    uint mX=volume.marginX, mY=volume.marginY;
    uint imX=X-2*mX, imY=Y-2*mY;
    Image target(imX,imY);
    if(volume.sampleSize==2) {
        const uint16* const source = (const Volume16&)volume + z*Y*X + mY*X + mX;
        for(uint y=0; y<imY; y++) for(uint x=0; x<imX; x++) target(x,y) = uint(source[y*X+x]) * (0xFF * volume.num) / volume.den;
    } else if(volume.sampleSize==4) {
        const uint32* const source = (const Volume32&)volume + z*Y*X + mY*X + mX;
        for(uint y=0; y<imY; y++) for(uint x=0; x<imX; x++) target(x,y) = uint(source[y*X+x]) * (0xFF * volume.num) / volume.den;
    } else error("Unsupported sample size", volume.sampleSize);
    return target;
}

/// Returns the square root of an image of a volume slice
Image squareRoot(const Volume& volume, uint z) {
    uint X=volume.x, Y=volume.y;
    uint mX=volume.marginX, mY=volume.marginY;
    uint imX=X-2*mX, imY=Y-2*mY;
    Image target(imX,imY);
    float scale = 0x100 * sqrt(float(volume.num) / float(volume.den));
    if(volume.sampleSize==2) {
        const uint16* const source = (const Volume16&)volume + z*Y*X + mY*X + mX;
        for(uint y=0; y<imY; y++) for(uint x=0; x<imX; x++) target(x,y) = uint8(sqrt(float(source[y*X+x])) * scale);
    } else if(volume.sampleSize==4) {
        const uint32* const source = (const Volume32&)volume + z*Y*X + mY*X + mX;
        for(uint y=0; y<imY; y++) for(uint x=0; x<imX; x++) target(x,y) = uint8(sqrt(float(source[y*X+x])) * scale);
    } else error("Unsupported sample size", volume.sampleSize);
    return target;
}
