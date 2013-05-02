#pragma once
#include "image.h"

struct Volume {
    Volume(){}
    Volume(uint sampleSize, uint x, uint y, uint z): data(sampleSize*x*y*z),x(x),y(y),z(z),sampleSize(sampleSize) {}

    uint64 size() { return x*y*z*sampleSize; }
    operator bool() { assert(data.size==size()); return data; }
    operator const struct Volume16&() const { assert(sampleSize==sizeof(uint16)); return (struct Volume16&)*this; }
    operator const struct Volume32&() const { assert(sampleSize==sizeof(uint32)); return (struct Volume32&)*this; }
    operator struct Volume16&() { assert(sampleSize==sizeof(uint16)); return (struct Volume16&)*this; }
    operator struct Volume32&() { assert(sampleSize==sizeof(uint32)); return (struct Volume32&)*this; }

    buffer<byte> data; // Samples ordered in Z slices, Y rows, X samples
    // Offset lookup tables for bricked volumes
    buffer<uint> offsetX;
    buffer<uint> offsetY;
    buffer<uint> offsetZ;
    uint x=0, y=0, z=0; // Sample count in each dimensions
    uint marginX=0, marginY=0, marginZ=0; // Margins to trim when processing volume
    int num=1, den=1; // Scale to apply to compute normalized values (data*num/den)
    uint sampleSize=0; // Sample integer size (in bytes)
    bool own=false;
};

struct Volume16 : Volume {
    Volume16(uint x, uint y, uint z) : Volume(sizeof(uint16),x,y,z) {}
    operator const uint16*() const { return (uint16*)data.data; }
    operator uint16*() { return (uint16*)data.data; }
};

struct Volume32 : Volume {
    Volume32(uint x, uint y, uint z): Volume(sizeof(uint32),x,y,z) {}
    operator const uint32*() const { return (uint32*)data.data; }
    operator uint32*() { return (uint32*)data.data; }
};

/// Precomputes a lookup table of bit interleaving (Morton aka z-order)
inline buffer<uint> interleavedLookup(uint size, uint offset, uint stride=3) {
    buffer<uint> lookup(size);
    for(uint i=0; i<size; i++) { lookup[i]=0; for(uint b=0, bits=i; bits!=0; bits>>=1, b++) { uint bit=bits&1; lookup[i] |= bit << (b*stride+offset); } }
    return lookup;
}

inline void interleavedLookup(Volume& target) {
    if(!target.offsetX) target.offsetX = interleavedLookup(target.x,0);
    if(!target.offsetY) target.offsetY = interleavedLookup(target.y,1);
    if(!target.offsetZ) target.offsetZ = interleavedLookup(target.z,2);
}

/// Returns an image of a volume slice
inline Image slice(const Volume& volume, uint z) {
    uint X=volume.x, Y=volume.y;
    Image target(X,Y);
    if(volume.sampleSize==2) {
        const uint16* const source = (const Volume16&)volume + z*Y*X;
        for(uint y=0; y<Y; y++) for(uint x=0; x<X; x++) target(x,y) = uint(source[y*X+x]) * (0x100 * volume.num) / volume.den;
    } else if(volume.sampleSize==4) {
        const uint32* const source = (const Volume32&)volume + z*Y*X;
        for(uint y=0; y<Y; y++) for(uint x=0; x<X; x++) target(x,y) = uint(source[y*X+x]) * (0x100 * volume.num) / volume.den;
    } else error("Unsupported sample size", volume.sampleSize);
    return target;
}

inline float sqrt(float f) { return __builtin_sqrtf(f); }
/// Returns the square root of an image of a volume slice
inline Image squareRoot(const Volume& volume, uint z) {
    uint X=volume.x, Y=volume.y;
    Image target(X,Y);
    float scale = 0x100 * sqrt(float(volume.num) / float(volume.den));
    if(volume.sampleSize==2) {
        const uint16* const source = (const Volume16&)volume + z*Y*X;
        for(uint y=0; y<Y; y++) for(uint x=0; x<X; x++) target(x,y) = uint8(sqrt(float(source[y*X+x])) * scale);
    } else if(volume.sampleSize==4) {
        const uint32* const source = (const Volume32&)volume + z*Y*X;
        for(uint y=0; y<Y; y++) for(uint x=0; x<X; x++) target(x,y) = uint8(sqrt(float(source[y*X+x])) * scale);
    } else error("Unsupported sample size", volume.sampleSize);
    return target;
}
