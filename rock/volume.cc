#include "volume.h"
#include "math.h"
#include "simd.h"
#include "thread.h"
#include "data.h"

/// Interleaves bits
static uint interleave(uint bits, uint offset, uint stride=3) { uint interleavedBits=0; for(uint b=0; bits!=0; bits>>=1, b++) interleavedBits |= (bits&1) << (b*stride+offset); return interleavedBits; }
/// Interleaves 3 coordinates
uint zOrder(int3 coordinates) { return interleave(coordinates.x, 0)|interleave(coordinates.y, 1)|interleave(coordinates.z, 2); }
/// Generates lookup tables of interleaved bits
static buffer<uint> interleavedLookup(uint size, uint offset, uint stride=3) { buffer<uint> lookup(size); for(uint i=0; i<size; i++) { lookup[i]=interleave(i,offset,stride); } return lookup; }

/// Pack interleaved bits
static uint pack(uint bits, uint offset, uint stride=3) { uint packedBits=0; bits>>=offset; for(uint b=0; bits!=0; bits>>=stride, b++) packedBits |= (bits&1) << b; return packedBits; }
/// Uninterleaves 3 coordinates
int3 zOrder(uint index) { return int3(pack(index,0),pack(index,1),pack(index,2)); }

void interleavedLookup(Volume& target) {
    assert(!target.tiled());
    target.offsetX = interleavedLookup(target.sampleCount.x,0);
    target.offsetY = interleavedLookup(target.sampleCount.y,1);
    target.offsetZ = interleavedLookup(target.sampleCount.z,2);
}

String volumeFormat(const Volume& volume) {
    String s; s << str(volume.sampleCount.x) << 'x' << str(volume.sampleCount.y) << 'x' << str(volume.sampleCount.z);
    if(volume.margin) s << '+' << str(volume.margin.x) << '+' << str(volume.margin.y) << '+' << str(volume.margin.z);
    s << '-' << hex(volume.maximum);
    if(volume.tiled()) s << "-tiled"_;
    if(volume.squared) s << "-squared"_;
    if(volume.floatingPoint) s << "-float"_;
    return s;
}

bool parseVolumeFormat(Volume& volume, const string& format) {
    TextData s (format);
    volume.sampleCount.x = s.mayInteger(); if(!s.match("x"_)) return false;
    volume.sampleCount.y = s.mayInteger(); if(!s.match("x"_)) return false;
    volume.sampleCount.z = s.mayInteger();
    if(s.match('+')) {
        volume.margin.x = s.mayInteger(); if(!s.match("+"_)) return false;
        volume.margin.y = s.mayInteger(); if(!s.match("+"_)) return false;
        volume.margin.z = s.mayInteger();
    }
    if(!s.match("-"_)) return false;
    volume.maximum = s.hexadecimal();
    if(s.match("-tiled"_)) interleavedLookup(volume); else { volume.offsetX=buffer<uint>(), volume.offsetY=buffer<uint>(), volume.offsetZ=buffer<uint>(); }
    if(s.match("-squared"_)) volume.squared=true;
    if(s.match("-float"_)) volume.floatingPoint=true;
    if(s) return false;
    return true;
}

uint maximum(const Volume16& source) {
    const uint16* const sourceData = source;
    const uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    v8hi maximum8 = {0,0,0,0,0,0,0,0};
    uint16 maximum=0;
    for(uint z : range(marginZ, Z-marginZ)) {
        const uint16* const sourceZ = sourceData + z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* const sourceZY = sourceZ + y*X;
            for(uint x=marginX; x<align(8,marginX); x++) maximum = max(maximum, sourceZY[x]); // Processes from margin to next aligned position
            for(uint x=align(8,marginX); x<floor(8,X-marginX); x+=8) maximum8 = max(maximum8, loada(sourceZY+x)); // Processes using SIMD (8x speedup)
            for(uint x=floor(8,X-marginX); x<X-marginX; x++) maximum = max(maximum, sourceZY[x]); // Processes from last aligned position to margin
        }
    }
    for(uint i: range(8)) maximum = max(maximum, ((uint16*)&maximum8)[i]);
    return maximum;
}

uint maximum(const Volume32& source) {
    const uint32* const sourceData = source;
    const uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    v4si maximum4 = {0,0,0,0};
    uint32 maximum=0;
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint32* const sourceZ = sourceData + z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint32* const sourceZY = sourceZ + y*X;
            for(uint x=marginX; x<align(4,marginX); x++) maximum = max(maximum, sourceZY[x]); // Processes from margin to next aligned position
            for(uint x=align(4,marginX); x<floor(4,X-marginX); x+=4) maximum4 = max(maximum4, loada(sourceZY+x)); // Processes using SIMD (4x speedup)
            for(uint x=floor(4,X-marginX); x<X-marginX; x++) maximum = max(maximum, sourceZY[x]); // Processes from last aligned position to margin
        }
    }
    for(uint i: range(4)) maximum = max(maximum, ((uint32*)&maximum4)[i]);
    return maximum;
}

Image slice(const Volume& source, real normalizedZ, bool normalize, bool gamma, bool cylinder) {
    int z = source.margin.z+normalizedZ*(source.sampleCount.z-2*source.margin.z-1);
    assert_(z >= source.margin.z && z<source.sampleCount.z-source.margin.z);
    //int z = normalizedZ*(source.sampleCount.z-1);
    return slice(source, z, normalize, gamma, cylinder);
}

Image slice(const Volume& source, int z, bool normalize, bool gamma, bool cylinder) {
    assert_(source.maximum);
    int X=source.sampleCount.x, Y=source.sampleCount.y;
    int marginX=source.margin.x, marginY=source.margin.y;
    Image target(X-2*marginX,Y-2*marginY, true);
    uint maximum = source.squared? round(sqrt(float(source.maximum))) : source.maximum;
    uint normalizeFactor = normalize ? maximum : 0xFF;
    if(!normalize && maximum==0xFFFF) { normalizeFactor=0xFF00; warn("16bit volume truncated to 8bit image slices"); }
    assert_(maximum*0xFF/normalizeFactor<=0xFF, maximum, "overflows 8bit");
    uint radiusSq = cylinder ? (X/2-marginX)*(Y/2-marginY) : -1;
    for(int y=marginY; y<Y-marginY; y++) for(int x=marginX; x<X-marginX; x++) {
         if(uint(sq(x-X/2)+sq(y-Y/2)) > radiusSq) { target(x-marginX,y-marginY) = byte4(0,0,0,0); continue; }
        uint value = 0;
        size_t index = source.index(x,y,z);
        if(source.sampleSize==1) value = ((byte*)source.data.data)[index];
        else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
        else if(source.sampleSize==3) { target(x-marginX,y-marginY) = ((bgr*)source.data.data)[index]; continue; } //FIXME: sRGB
        else if(source.sampleSize==4) value = ((uint32*)source.data.data)[index];
        else error("source.sampleSize"_,source.sampleSize);
        uint linear8 = (source.squared ? round(sqrt(float(value))) : value) * 0xFF / normalizeFactor;
        extern uint8 sRGB_lookup[256]; //FIXME: unnecessary quantization loss on rounding linear values to 8bit
        uint sRGB8 = gamma ? sRGB_lookup[linear8] : linear8; // !gamma: abusing sRGB standard to store linear values
        target(x-marginX,y-marginY) = byte4(sRGB8, sRGB8, sRGB8, 0xFF);
    }
    return target;
}
