#include "volume.h"
#include "simd.h"
#include "process.h"
#include "data.h"

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
    assert(!s);
}

uint maximum(const Volume16& source) {
    const uint16* const sourceData = source;
    const uint X=source.x, Y=source.y, Z=source.z, XY = X*Y;
    int marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    v8hi maximum8 = {0,0,0,0,0,0,0,0};
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint16* const sourceZ = sourceData + z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* const sourceZY = sourceZ + y*X;
            for(uint x=align(8,marginX); x<floor(8,X-marginX); x+=8) maximum8 = max(maximum8, loada(sourceZY+x));
        }
    }
    uint16 maximum=0; for(uint i: range(8)) maximum = max(maximum, ((uint16*)&maximum8)[i]);
    return maximum;
}

uint maximum(const Volume32& source) {
    const uint32* const sourceData = source;
    const uint X=source.x, Y=source.y, Z=source.z, XY = X*Y;
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
    uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    const uint16* const sourceData = source;
    interleavedLookup(target);
    uint16* const targetData = target;
    const uint* const offsetX = target.offsetX;
    const uint* const offsetY = target.offsetY;
    const uint* const offsetZ = target.offsetZ;

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

void crop(Volume16& target, const Volume16& source, uint x1, uint y1, uint z1, uint x2, uint y2, uint z2) {
    uint X=x2-x1, Y=y2-y1, Z=z2-z1, XY=X*Y;
    target.x=X, target.y=Y, target.z=Z;
    target.marginX=target.marginY=target.marginZ=0; // Assumes crop outside margins
    assert_(source.offsetX && source.offsetY && source.offsetZ);
    const uint* const offsetX = source.offsetX;
    const uint* const offsetY = source.offsetY;
    const uint* const offsetZ = source.offsetZ;
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
    uint i=4;
    do { target[--i]="0123456789"[n%10]; n /= 10; } while( n!=0 );
    while(i>0) target[--i]=' ';
}

void toASCII(Volume& target, const Volume16& source) {
    uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    assert_(source.offsetX && source.offsetY && source.offsetZ);
    assert_(!target.offsetX && !target.offsetY && !target.offsetZ);
    const uint* const offsetX = source.offsetX;
    const uint* const offsetY = source.offsetY;
    const uint* const offsetZ = source.offsetZ;
    const uint16* const sourceData = source;
    typedef char Line[20];
    Line* const targetData = (Line*)target.data.data;

    for(uint z=0; z<Z; z++) {
        const uint16* const sourceZ = sourceData + offsetZ[z];
        Line* const targetZ = targetData + z*XY;
        for(uint y=0; y<Y; y++) {
            const uint16* const sourceZY = sourceZ + offsetY[y];
            Line* const targetZY = targetZ + y*X;
            for(uint x=0; x<X; x++) {
                Line& line = targetZY[x];
                itoa(line, x);
                line[4]=' ';
                itoa(line+5, y);
                line[9]=' ';
                itoa(line+10,z);
                line[14]=' ';
                itoa(line+15,sourceZY[offsetX[x]]);
                line[19]='\n';
            }
        }
    }
}

Image slice(const Volume& volume, uint z, bool cylinder) {
    int X=volume.x, Y=volume.y, XY=X*Y;
    Image target(X,Y);
    assert(X==Y && volume.marginX==volume.marginY);
    uint radiusSq = cylinder ? (X/2-volume.marginX)*(X/2-volume.marginX) : -1;
    const uint* const offsetX = volume.offsetX, *offsetY = volume.offsetY, *offsetZ = volume.offsetZ;
    for(int y=0; y<Y; y++) for(int x=0; x<X; x++) {
        if(uint((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2)) > radiusSq) { target(x,y) = byte4(0,0,0,0x80); continue; }
        uint source = 0;
        uint offset = offsetX ? offsetX[x] + offsetY[y] + offsetZ[z] : z*XY + y*X + x;
        if(volume.sampleSize==1) source = ((byte*)volume.data.data)[offset];
        else if(volume.sampleSize==2) source = ((uint16*)volume.data.data)[offset];
        else if(volume.sampleSize==3) { target(x,y) = ((bgr*)volume.data.data)[offset]; continue; }
        else if(volume.sampleSize==4) source = ((uint32*)volume.data.data)[offset];
        else error(volume.sampleSize);
        uint intensity = volume.squared ? round(sqrt(source)) * 0xFF / round(sqrt(volume.maximum)) : source * 0xFF / volume.maximum;
        target(x,y) = byte4(intensity, intensity, intensity, 0xFF);
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
