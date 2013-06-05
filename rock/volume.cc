#include "volume.h"
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
    if(!target.offsetX) target.offsetX = interleavedLookup(target.sampleCount.x,0);
    if(!target.offsetY) target.offsetY = interleavedLookup(target.sampleCount.y,1);
    if(!target.offsetZ) target.offsetZ = interleavedLookup(target.sampleCount.z,2);
}

string volumeFormat(const Volume& volume) {
    string s; s << str(volume.sampleCount.x) << 'x' << str(volume.sampleCount.y) << 'x' << str(volume.sampleCount.z);
    if(volume.margin) s << '+' << str(volume.margin.x) << '+' << str(volume.margin.y) << '+' << str(volume.margin.z);
    s << '-' << hex(volume.maximum);
    if(volume.offsetX||volume.offsetY||volume.offsetZ) s << "-tiled"_;
    if(volume.squared) s << "-squared"_;
    return s;
}

bool parseVolumeFormat(Volume& volume, const ref<byte>& format) {
    TextData s (format);
    volume.sampleCount.x = s.integer(); if(!s.match("x"_)) return false;
    volume.sampleCount.y = s.integer(); if(!s.match("x"_)) return false;
    volume.sampleCount.z = s.integer();
    if(s.match('+')) {
        volume.margin.x = s.integer(); if(!s.match("+"_)) return false;
        volume.margin.y = s.integer(); if(!s.match("+"_)) return false;
        volume.margin.z = s.integer();
    }
    if(!s.match("-"_)) return false;
    volume.maximum = s.hexadecimal();
    if(s.match("-tiled"_)) interleavedLookup(volume); else { volume.offsetX=buffer<uint>(), volume.offsetY=buffer<uint>(), volume.offsetZ=buffer<uint>(); }
    if(s.match("-squared"_)) volume.squared=true;
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

void pack(Volume16& target, const Volume32& source) {
    const uint32* const sourceData = source;
    uint16* const targetData = target;
    uint64 size = source.size();
    for(uint i=0; i<size; i+=8) storea(targetData+i, packus(loada(sourceData+i),loada(sourceData+i+4)));
    target.maximum=source.maximum;
}

void tile(Volume16& target, const Volume16& source) {
    const uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    const uint16* const sourceData = source;
    interleavedLookup(target);
    uint16* const targetData = target;
    const uint* const offsetX = target.offsetX, *offsetY = target.offsetY, *offsetZ = target.offsetZ;

    for(uint z=0; z<Z; z++) {
        const uint16* const sourceZ = sourceData+z*XY;
        uint16* const targetZ = targetData+offsetZ[z];
        for(uint y=0; y<Y; y++) {
            const uint16* const sourceZY = sourceZ+y*X;
            uint16* const targetZY = targetZ+offsetY[y];
            //for(uint x=0; x<X; x+=2) *(uint32*)(targetZY+offsetX[x]) = *(uint32*)(sourceZY+x);
            for(uint x=0; x<X; x++) targetZY[offsetX[x]] = sourceZY[x];
        }
    }
    target.copyMetadata(source);
}

template<Type T> void setBorders(VolumeT<T>& target) {
    const uint X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY=X*Y;
    int marginX=target.margin.x, marginY=target.margin.y, marginZ=target.margin.z;
    T* const targetData = target;
    assert(!target.offsetX && !target.offsetY && !target.offsetZ);
    for(uint z=0; z<Z; z++) {
        T* const targetZ = targetData + z*XY;
        for(uint y=0; y<Y; y++) {
            T* const targetZY = targetZ + y*X;
            for(uint x=0; x<=marginX; x++) targetZY[x] = x*x;
            for(uint x=X-marginX; x<X; x++) targetZY[x] = x*x;
        }
        for(uint x=0; x<X; x++) {
            T* const targetZX = targetZ + x;
            for(uint y=0; y<=marginY; y++) targetZX[y*X] = x*x;
            for(uint y=Y-marginY; y<Y; y++) targetZX[y*X] = x*x;
        }
    }
    for(uint y=0; y<Y; y++) {
        T* const targetY = targetData + y*X;
        for(uint x=0; x<X; x++) {
            T* const targetYX = targetY + x;
            for(uint z=0; z<=marginZ; z++) targetYX[z*XY] = x*x;
            for(uint z=Z-marginZ; z<Z; z++) targetYX[(Z-marginZ-1)*XY] = x*x;
        }
    }
}

template void setBorders<uint32>(VolumeT<uint32>& target);

#if 0
void crop(Volume16& target, const Volume16& source, uint x1, uint y1, uint z1, uint x2, uint y2, uint z2) {
    uint X=x2-x1, Y=y2-y1, Z=z2-z1, XY=X*Y;
    target.sampleCount.x=X, target.sampleCount.y=Y, target.sampleCount.z=Z;
    target.margin.x=target.margin.y=target.margin.z=0; // Assumes crop outside margins
    assert_(source.offsetX && source.offsetY && source.offsetZ);
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    for(uint z=0; z<Z; z++) {
        const uint16* const sourceZ = sourceData + offsetZ[z1+z];
        uint16* const targetZ = targetData + z*XY;
        for(uint y=0; y<Y; y++) {
            const uint16* const sourceZY = sourceZ + offsetY[y1+y];
            uint16* const targetZY = targetZ + y*X;
            for(uint x=0; x<X; x++) targetZY[x]=sourceZY[offsetX[x1+x]];
        }
    }
}
#endif

void downsample(Volume16& target, const Volume16& source) {
    assert(!source.offsetX && !source.offsetY && !source.offsetZ);
    int X = source.sampleCount.x, Y = source.sampleCount.y, Z = source.sampleCount.z, XY = X*Y;
    target.sampleCount = source.sampleCount/2;
    target.data.size /= 8;
    target.margin = source.margin/2;
    target.maximum=source.maximum;
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

/// Square roots all values
void squareRoot(Volume8& target, const Volume16& source, bool normalize) {
    uint X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY=X*Y;
    interleavedLookup(target);
    const uint* const offsetX = target.offsetX, *offsetY = target.offsetY, *offsetZ = target.offsetZ;
    target.maximum = normalize ? 0xFF : round(sqrt((float)source.maximum)), target.squared=false;
    assert_(target.maximum<0x100);

    const uint16* const sourceData = source;
    uint8* const targetData = target;
    parallel(Z, [&](uint, uint z) {
        if(source.offsetX || source.offsetY || source.offsetZ) {
            assert(source.offsetX && source.offsetY && source.offsetZ);
            const uint16* const sourceZ = sourceData + offsetZ[z];
            uint8* const targetZ = targetData + offsetZ[z];
            for(uint y=0; y<Y; y++) {
                const uint16* const sourceZY = sourceZ + offsetY[y];
                uint8* const targetZY = targetZ + offsetY[y];
                for(uint x=0; x<X; x++) {
                    uint value = round(sqrt(float(sourceZY[offsetX[x]])));
                    if(normalize) value = value * 0xFF / target.maximum;
                    targetZY[offsetX[x]] = value;
                }
            }
        } else {
            assert(!source.offsetX && !source.offsetY && !source.offsetZ);
            const uint16* const sourceZ = sourceData + z*XY;
            uint8* const targetZ = targetData + offsetZ[z];
            for(uint y=0; y<Y; y++) {
                const uint16* const sourceZY = sourceZ + y*X;
                uint8* const targetZY = targetZ + offsetY[y];
                for(uint x=0; x<X; x++) {
                    uint value = round(sqrt(float(sourceZY[x])));
                    if(normalize) value = value * 0xFF / target.maximum;
                    targetZY[offsetX[x]] = value;
                }
            }
        }
    } );
}

Image slice(const Volume& source, float normalizedZ, bool cylinder) {
    int z = source.margin.z+normalizedZ*(source.sampleCount.z-2*source.margin.z-1);
    assert_(z >= source.margin.z && z<source.sampleCount.z-source.margin.z);
    return slice(source, z, cylinder);
}

Image slice(const Volume& source, int z, bool cylinder) {
    int X=source.sampleCount.x, Y=source.sampleCount.y, XY=X*Y;
    Image target(X,Y);
    int unused marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    //assert(X==Y && marginX==marginY);
    uint radiusSq = cylinder ? (X/2-marginX)*(Y/2-marginY) : -1;
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    for(int y=0; y<Y; y++) for(int x=0; x<X; x++) {
        if(uint((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2)) > radiusSq) { target(x,y) = byte4(0,0,0,0); continue; }
        uint value = 0;
        uint index = offsetX ? offsetX[x] + offsetY[y] + offsetZ[z] : z*XY + y*X + x;
        if(source.sampleSize==1) value = ((byte*)source.data.data)[index];
        else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
        else if(source.sampleSize==3) { target(x,y) = ((bgr*)source.data.data)[index]; continue; } //FIXME: sRGB
        else if(source.sampleSize==4) value = ((uint32*)source.data.data)[index];
        else error("source.sampleSize"_,source.sampleSize);
        uint linear8 = source.squared ? round(sqrt(value)) * 0xFF / round(sqrt(source.maximum)) : value * 0xFF / source.maximum;
        assert(linear8<0x100 || x<marginX || y<marginY || (int)z<marginZ, linear8, value, source.maximum, x, y, z);
        linear8 = min<uint>(0xFF, linear8); //FIXME
        extern uint8 sRGB_lookup[256];
        uint sRGB8 = sRGB_lookup[linear8];
        target(x,y) = byte4(sRGB8, sRGB8, sRGB8, 0xFF);
    }
    return target;
}
