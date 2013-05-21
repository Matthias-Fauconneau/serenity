#include "volume.h"
#include "simd.h"
#include "process.h"
#include "data.h"
#include "time.h"

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

string volumeFormat(const Volume& volume) {
    string s; s << str(volume.x) << 'x' << str(volume.y) << 'x' << str(volume.z);
    if(volume.marginX||volume.marginY||volume.marginZ) s << '+' << str(volume.marginX) << '+' << str(volume.marginY) << '+' << str(volume.marginZ);
    s << '-' << hex(volume.maximum);
    if(volume.offsetX||volume.offsetY||volume.offsetZ) s << "-tiled"_;
    if(volume.squared) s << "-squared"_;
    return s;
}

void parseVolumeFormat(Volume& volume, const ref<byte>& path) {
    TextData s ( section(path,'.',-2,-1) );
    volume.x = s.integer(); s.skip("x"_);
    volume.y = s.integer(); s.skip("x"_);
    volume.z = s.integer();
    if(s.match('+')) {
        volume.marginX = s.integer(); s.skip("+"_);
        volume.marginY = s.integer(); s.skip("+"_);
        volume.marginZ = s.integer();
    }
    s.skip("-"_);
    volume.maximum = s.hexadecimal();
    if(s.match("-tiled"_)) interleavedLookup(volume); else { free(volume.offsetX), free(volume.offsetY), free(volume.offsetZ); }
    if(s.match("-squared"_)) volume.squared=true;
    s.match("-unused"_);
    assert(!s);
}

uint maximum(const Volume16& source) {
    const uint16* const sourceData = source;
    const uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    int marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
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
    const uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    int marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    assert((X-2*marginX)%4==0 && marginX%4==0);
    v4si maximum4 = {0,0,0,0};
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint32* const sourceZ = sourceData + z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint32* const sourceZY = sourceZ + y*X;
            for(uint x=marginX; x<X-marginX; x+=4) maximum4 = max(maximum4, loada(sourceZY+x));
        }
    }
    uint32 maximum=0; for(uint i: range(4)) maximum = max(maximum, ((uint32*)&maximum4)[i]);
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
    const uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
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

template<Type T> void clearMargins(VolumeT<T>& target, uint value) {
    Time time;
    const uint X=target.x, Y=target.y, Z=target.z, XY=X*Y;
    int marginX=target.marginX, marginY=target.marginY, marginZ=target.marginZ;
    T* const targetData = target;
    if(target.offsetX || target.offsetY || target.offsetZ) {
        const uint* const offsetX = target.offsetX, *offsetY = target.offsetY, *offsetZ = target.offsetZ;
        assert(offsetX && offsetY && offsetZ);
        for(uint z=0; z<Z; z++) {
            T* const targetZ = targetData + offsetZ[z];
            for(uint y=0; y<Y; y++) {
                T* const targetZY = targetZ + offsetY[y];
                for(uint x=0; x<=marginX; x++) targetZY[offsetX[x]] = value;
                for(uint x=X-marginX; x<X; x++) targetZY[offsetX[x]] = value;
            }
            for(uint x=0; x<X; x++) {
                T* const targetZX = targetZ + offsetX[x];
                for(uint y=0; y<=marginY; y++) targetZX[offsetY[y]] = value;
                for(uint y=Y-marginY; y<Y; y++) targetZX[offsetY[y]] = value;
            }
        }
        for(uint y=0; y<Y; y++) {
            T* const targetY = targetData + offsetY[y];
            for(uint x=0; x<X; x++) {
                T* const targetYX = targetY + offsetX[x];
                for(uint z=0; z<=marginZ; z++) targetYX[z] = value;
                for(uint z=Z-marginZ; z<Z; z++) targetYX[z] = value;
            }
        }
    }
    else {
        for(uint z=0; z<Z; z++) {
            T* const targetZ = targetData + z*XY;
            for(uint y=0; y<Y; y++) {
                T* const targetZY = targetZ + y*X;
                for(uint x=0; x<=marginX; x++) targetZY[x] = value;
                for(uint x=X-marginX; x<X; x++) targetZY[x] = value;
            }
            for(uint x=0; x<X; x++) {
                T* const targetZX = targetZ + x;
                for(uint y=0; y<=marginY; y++) targetZX[y*X] = value;
                for(uint y=Y-marginY; y<Y; y++) targetZX[y*X] = value;
            }
        }
        for(uint y=0; y<Y; y++) {
            T* const targetY = targetData + y*X;
            for(uint x=0; x<X; x++) {
                T* const targetYX = targetY + x;
                for(uint z=0; z<=marginZ; z++) targetYX[z*XY] = value;
                for(uint z=Z-marginZ; z<Z; z++) targetYX[(Z-marginZ-1)*XY] = value;
            }
        }
    }
    log("clear",time); //FIXME
}
template void clearMargins<uint16>(VolumeT<uint16>& target, uint value);
template void clearMargins<uint32>(VolumeT<uint32>& target, uint value);

void crop(Volume16& target, const Volume16& source, uint x1, uint y1, uint z1, uint x2, uint y2, uint z2) {
    uint X=x2-x1, Y=y2-y1, Z=z2-z1, XY=X*Y;
    target.x=X, target.y=Y, target.z=Z;
    target.marginX=target.marginY=target.marginZ=0; // Assumes crop outside margins
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

void downsample(Volume16& target, const Volume16& source) {
    assert(!source.offsetX && !source.offsetY && !source.offsetZ);
    int X = source.x, Y = source.y, Z = source.z, XY = X*Y;
    target.x = X/2, target.y = Y/2, target.z = Z/2; target.marginX=source.marginX/2, target.marginY=source.marginZ/2, target.marginX=source.marginZ/2, target.maximum=source.maximum;
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

inline void itoa(byte* target, uint n) {
    assert(n<1000);
    uint i=4;
    do { target[--i]="0123456789"[n%10]; n /= 10; } while( n!=0 );
    while(i>0) target[--i]=' ';
}

void toASCII(Volume& target, const Volume& source) {
    uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    uint marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    assert_(!target.offsetX && !target.offsetY && !target.offsetZ);
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    typedef char Line[20];
    Line* const targetData = (Line*)target.data.data;
    for(uint z=0; z<Z; z++) {
        Line* const targetZ = targetData + z*XY;
        for(uint y=0; y<Y; y++) {
            Line* const targetZY = targetZ + y*X;
            for(uint x=0; x<X; x++) {
                uint value = 0;
                if(x >= marginX && x<X-marginX && y >= marginY && y<Y-marginY && z >= marginZ && z<Z-marginZ) {
                    uint offset = offsetX ? offsetX[x] + offsetY[y] + offsetZ[z] : z*XY + y*X + x;
                    if(source.sampleSize==1) value = ((byte*)source.data.data)[offset];
                    else if(source.sampleSize==2) value = ((uint16*)source.data.data)[offset];
                    else if(source.sampleSize==4) value = ((uint32*)source.data.data)[offset];
                    else error(source.sampleSize);
                    assert(value <= source.maximum, value, source.maximum);
                }

                Line& line = targetZY[x];
                itoa(line, x);
                line[4]=' ';
                itoa(line+5, y);
                line[9]=' ';
                itoa(line+10,z);
                line[14]=' ';
                itoa(line+15, min<uint>(0xFF, value * 0xFF / source.maximum)); //FIXME
                line[19]='\n';
            }
        }
    }
}

Image slice(const Volume& source, float normalizedZ, bool cylinder) {
    uint z = source.marginZ+(source.z-2*source.marginZ-1)*normalizedZ;
    assert_(z >= source.marginZ && z<source.z-source.marginZ);
    return slice(source, (int)z, cylinder);
}

Image slice(const Volume& source, int z, bool cylinder) {
    int X=source.x, Y=source.y, XY=X*Y;
    Image target(X,Y);
    int unused marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    assert(X==Y && marginX==marginY);
    uint radiusSq = cylinder ? (X/2-marginX)*(X/2-marginX) : -1;
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    for(int y=0; y<Y; y++) for(int x=0; x<X; x++) {
        if(uint((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2)) > radiusSq) { target(x,y) = byte4(0,0,0,0x80); continue; }
        uint value = 0;
        uint offset = offsetX ? offsetX[x] + offsetY[y] + offsetZ[z] : z*XY + y*X + x;
        if(source.sampleSize==1) value = ((byte*)source.data.data)[offset];
        else if(source.sampleSize==2) value = ((uint16*)source.data.data)[offset];
        else if(source.sampleSize==3) { target(x,y) = ((bgr*)source.data.data)[offset]; continue; } //FIXME: sRGB
        else if(source.sampleSize==4) value = ((uint32*)source.data.data)[offset];
        else error(source.sampleSize);
        uint linear8 = source.squared ? round(sqrt(value)) / round(sqrt(source.maximum)) * 0xFF : value * 0xFF / source.maximum;
        //assert(linear8<0x100 || x<marginX || y<marginY || z<marginZ, linear8, value, source.maximum, x, y, z);
        linear8 = min<uint>(0xFF, linear8); //FIXME
        extern uint8 sRGB_lookup[256];
        uint sRGB8 = sRGB_lookup[linear8];
        target(x,y) = byte4(sRGB8, sRGB8, sRGB8, 0xFF);
    }
    return target;
}

void colorize(Volume24& target, const Volume32& binary, const Volume16& intensity) {
    assert(!binary.offsetX && !binary.offsetY && !binary.offsetZ);
    int X = binary.x, Y = binary.y, Z = binary.z, XY = X*Y;
    const uint32* const binaryData = binary;
    const uint16* const intensityData = intensity;
    const uint maximum = intensity.maximum;
    bgr* const targetData = target;
    parallel(Z, [&](uint, uint z) {
        const uint32* const binaryZ = binaryData+z*XY;
        const uint16* const intensityZ = intensityData+z*XY;
        bgr* const targetZ = targetData+z*XY;
        for(int y=0; y<Y; y++) {
            const uint32* const binaryZY = binaryZ+y*X;
            const uint16* const intensityZY = intensityZ+y*X;
            bgr* const targetZY = targetZ+y*X;
            for(int x=0; x<X; x++) {
                uint8 c = 0xFF*intensityZY[x]/maximum;
                targetZY[x] = binaryZY[x]==0xFFFFFFFF ? bgr{0,c,0} : bgr{0,0,c};
            }
        }
    });
    target.maximum=0xFF;
}
