#include "volume.h"
#include "math.h"
#include "simd.h"
#include "thread.h"
#include "data.h"

/// Interleaves bits
static uint64 interleave(uint64 bits, uint64 offset, uint stride=3) { uint64 interleavedBits=0; for(uint b=0; bits!=0; bits>>=1, b++) interleavedBits |= (bits&1) << (b*stride+offset); return interleavedBits; }
/// Interleaves 3 coordinates
uint64 zOrder(int3 coordinates) { return interleave(coordinates.x, 0)|interleave(coordinates.y, 1)|interleave(coordinates.z, 2); }
/// Generates lookup tables of interleaved bits
static buffer<uint64> interleavedLookup(uint size, uint offset, uint stride=3) { buffer<uint64> lookup(size); for(uint i=0; i<size; i++) { lookup[i]=interleave(i,offset,stride); } return lookup; }

/// Pack interleaved bits
static uint pack(uint64 bits, uint offset, uint stride=3) { uint packedBits=0; bits>>=offset; for(uint b=0; bits!=0; bits>>=stride, b++) packedBits |= (bits&1) << b; return packedBits; }
/// Uninterleaves 3 coordinates
int3 zOrder(uint64 index) { return int3(pack(index,0),pack(index,1),pack(index,2)); }

void interleavedLookup(Volume& target) {
    if(target.tiled()) return;
    target.offsetX = interleavedLookup(target.sampleCount.x,0);
    target.offsetY = interleavedLookup(target.sampleCount.y,1);
    target.offsetZ = interleavedLookup(target.sampleCount.z,2);
}

String volumeFormat(const Volume& volume) {
    String s; s << str(volume.sampleCount.x) << 'x' << str(volume.sampleCount.y) << 'x' << str(volume.sampleCount.z);
    s << '+' << str(volume.margin.x) << '+' << str(volume.margin.y) << '+' << str(volume.margin.z);
    s << '-' << hex(volume.maximum);
    if(volume.tiled()) s << "-tiled"_;
    if(volume.squared) s << "-squared"_;
    if(volume.floatingPoint) s << "-float"_;
    if(volume.field) s << "-"_ << volume.field;
    if(volume.origin) s << '+' << str(volume.origin.x) << '+' << str(volume.origin.y) << '+' << str(volume.origin.z);
    return s;
}

bool parseVolumeFormat(Volume& volume, const string& format) {
    TextData s (format);
    volume.sampleCount.x = s.mayInteger(); if(!s.match("x"_)) return false;
    volume.sampleCount.y = s.mayInteger(); if(!s.match("x"_)) return false;
    volume.sampleCount.z = s.mayInteger();
    if(!s.match('+')) return false;
    volume.margin.x = s.mayInteger(); if(!s.match("+"_)) return false;
    volume.margin.y = s.mayInteger(); if(!s.match("+"_)) return false;
    volume.margin.z = s.mayInteger();
    if(!s.match('-')) return false;
    volume.maximum = s.hexadecimal();
    if(s.match("-tiled"_)) interleavedLookup(volume); else { volume.offsetX=buffer<uint64>(), volume.offsetY=buffer<uint64>(), volume.offsetZ=buffer<uint64>(); }
    if(s.match("-squared"_)) volume.squared=true;
    if(s.match("-float"_)) volume.floatingPoint=true;
    if(s.match('-')) volume.field = String(s.whileNot('+'));
    if(s.match('+')) {
        volume.origin.x = s.mayInteger(true); if(!s.match('+')) return false;
        volume.origin.y = s.mayInteger(true); if(!s.match('+')) return false;
        volume.origin.z = s.mayInteger(true);
    }
    if(s) return false;
    return true;
}

uint minimum(const Volume16& source) {
    const uint16* const sourceData = source;
    const uint64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    const int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    v8hi minimum8 = {~0,~0,~0,~0,~0,~0,~0,~0};
    uint16 minimum = 0xFFFF;
    for(uint z : range(marginZ, Z-marginZ)) {
        const uint16* const sourceZ = sourceData + z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* const sourceZY = sourceZ + y*X;
            for(uint x=marginX; x<align(8,marginX); x++) minimum = min(minimum, sourceZY[x]); // Processes from margin to next aligned position
            for(uint x=align(8,marginX); x<floor(8,X-marginX); x+=8) minimum8 = min(minimum8, loada(sourceZY+x)); // Processes using SIMD (8x speedup)
            for(uint x=floor(8,X-marginX); x<X-marginX; x++) minimum = min(minimum, sourceZY[x]); // Processes from last aligned position to margin
        }
    }
    for(uint i: range(8)) minimum = min(minimum, ((uint16*)&minimum8)[i]);
    return minimum;
}

uint maximum(const Volume16& source) {
    const uint16* const sourceData = source;
    const uint64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    const int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
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

Image slice(const Volume& source, real normalizedZ, bool cylinder, bool normalize, bool gamma, bool invert, bool binary) {
    int z = source.margin.z+normalizedZ*(source.sampleCount.z-2*source.margin.z-1);
    assert_(z >= source.margin.z && z<source.sampleCount.z-source.margin.z, normalizedZ, source.margin, source.sampleCount, z);
    return slice(source, z, cylinder, normalize, gamma, invert, binary);
}

Image slice(const Volume& source, int z, bool cylinder, bool normalize, bool gamma, bool invert, bool binary) {
    assert_(source.maximum);
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y;
    const int marginX=source.margin.x, marginY=source.margin.y;
    Image target(X-2*marginX,Y-2*marginY, true);
    uint maximum = source.squared? round(sqrt(float(source.maximum))) : source.maximum;
    uint normalizeFactor = normalize ? maximum : 0xFF;
    if(!normalize && maximum>0x8000) { normalizeFactor=0xFF00; static int unused once = (warn("16bit volume truncated to 8bit image slices"_),0); }
    assert_(maximum*0xFF/normalizeFactor<=0xFF, maximum, "overflows 8bit (automatic 16bit to 8bit truncation activates only for maximum<=0x8000");
    float radiusSq = ((X-1)/2.0-marginX)*((Y-1)/2.0-marginY);
    for(int y=marginY; y<Y-marginY; y++) for(int x=marginX; x<X-marginX; x++) {
        if(cylinder && sq(x-(X-1)/2.f)+sq(y-(Y-1)/2.f) > radiusSq) { target(x-marginX,y-marginY) = invert ? byte4(0xFF,0xFF,0xFF,0) : byte4(0,0,0,0xFF/*Avoids transparent window*/); continue; }
        uint value = 0;
        size_t index = source.index(x,y,z);
        if(source.sampleSize==1) value = ((uint8*)source.data.data)[index];
        else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
        else if(source.sampleSize==3) { target(x-marginX,y-marginY) = ((bgr*)source.data.data)[index]; continue; } //FIXME: sRGB
        //else if(source.sampleSize==4) value = ((uint32*)source.data.data)[index];
        else if(source.sampleSize==4) { short2 xyz=((short2*)source.data.data)[index]; target(x-marginX,y-marginY) = bgr{uint8(xyz.x/2),uint8(xyz.y/2),0}; continue; } //FIXME: sRGB
        else if(source.sampleSize==6) { short3 xyz=((short3*)source.data.data)[index]; target(x-marginX,y-marginY) = bgr{uint8(xyz.x/2),uint8(xyz.y/2),uint8(xyz.z/2)}; continue; } //FIXME: sRGB
        else error("source.sampleSize"_,source.sampleSize);
        uint linear8 = (source.squared ? round(sqrt(float(value))) : value) * 0xFF / normalizeFactor;
        if(binary) linear8 = linear8 ? 0xFF : 0;
        if(invert) linear8 = 0xFF-linear8;
        extern uint8 sRGB_lookup[256]; //FIXME: unnecessary quantization loss on rounding linear values to 8bit
        uint sRGB8 = gamma ? sRGB_lookup[linear8] : linear8; // !gamma: abusing sRGB standard to store linear values
        target(x-marginX,y-marginY) = byte4(sRGB8, sRGB8, sRGB8, 0xFF);
    }
    return target;
}

Image16 slice(const Volume& source, int z) {
    assert_(source.maximum);
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y;
    //const int marginX=source.margin.x, marginY=source.margin.y;
    const int marginX=0, marginY=0;
    Image16 target(X-2*marginX,Y-2*marginY);
    for(int y=marginY; y<Y-marginY; y++) for(int x=marginX; x<X-marginX; x++) {
        uint value = 0;
        size_t index = source.index(x,y,z);
        if(source.sampleSize==1) value = ((byte*)source.data.data)[index];
        else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
        else error("source.sampleSize"_,source.sampleSize);
        target(x-marginX,y-marginY) = value;
    }
    return target;
}
