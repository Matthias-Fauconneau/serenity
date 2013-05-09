#include "volume.h"
#include "simd.h"
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
    if(volume.squared) s << "-squared";
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
    volume.sampleSize = align(8, nextPowerOfTwo(log2(nextPowerOfTwo((volume.maximum+1))))) / 8; // Minimum sample size to encode maximum value (in 2ⁿ bytes)
    if(s.match("-tiled"_)) interleavedLookup(volume); else { free(volume.offsetX), free(volume.offsetY), free(volume.offsetZ); }
    if(s.match("-squared"_)) volume.squared=true;
    assert(!s);
}

uint maximum(const Volume16& source) {
    const uint16* const sourceData = source;
    uint64 size = source.size();
    v8hi maximum8 = {};
    for(uint i=0; i<size; i+=8) maximum8 = max(maximum8, loada(sourceData+i));
    uint16 maximum=0; for(uint i: range(8)) maximum = max(maximum, ((uint16*)&maximum8)[i]);
    return maximum;
}

uint maximum(const Volume32& source) {
    const uint32* const sourceData = source;
    uint64 size = source.size();
    v4si maximum4 = {};
    for(uint i=0; i<size; i+=8) maximum4 = max(maximum4, loada(sourceData+i));
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

void toASCII(Volume& target, const Volume16& source) {
    uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    assert_(!source.offsetX && !source.offsetY && !source.offsetZ);
    const uint16* const sourceData = source;
    typedef char line[20];
    line* const targetData = (line*)target.data.data;

    for(uint z=0; z<Z; z++) {
        const uint16* const sourceZ = sourceData + z*XY;
        line* const targetZ = targetData + z*XY;
        for(uint y=0; y<Y; y++) {
            const uint16* const sourceZY = sourceZ + y*X;
            line* const targetZY = targetZ + y*X;
            for(uint x=0; x<X; x++) {
                string s = dec(x,4)+" "_+dec(y,4)+" "_+dec(z,4)+" "_+dec(sourceZY[x],4)+"\n"_; //FIXME: Extremely inefficient but who cares !
                assert(s.size==20);
                copy(targetZY[x], s.data, 20);
            }
        }
    }
}

Image slice(const Volume& volume, uint z) {
    uint X=volume.x, Y=volume.y;
    uint mX=volume.marginX, mY=volume.marginY;
    uint imX=X-2*mX, imY=Y-2*mY;
    Image target(imX,imY);
    const uint* const offsetX = volume.offsetX;
    const uint* const offsetY = volume.offsetY;
    const uint* const offsetZ = volume.offsetZ;
    if(volume.sampleSize==1) {
        if(offsetX || offsetY || offsetZ) {
            const uint8* const sourceZ = (const Volume8&)volume + offsetZ[z];
            for(uint y=0; y<imY; y++) {
                const uint8* const sourceY = sourceZ + offsetY[y];
                for(uint x=0; x<imX; x++) target(x,y) = uint(sourceY[offsetX[x]]) * 0xFF / volume.maximum;
            }
        }
    }
    else if(volume.sampleSize==2) {
        if(offsetX || offsetY || offsetZ) {
            const uint16* const sourceZ = (const Volume16&)volume + offsetZ[z];
            for(uint y=0; y<imY; y++) {
                const uint16* const sourceY = sourceZ + offsetY[y];
                for(uint x=0; x<imX; x++) target(x,y) = uint(sourceY[offsetX[x]]) * 0xFF / volume.maximum;
            }
        } else {
            const uint16* const source = (const Volume16&)volume + z*Y*X + mY*X + mX;
            for(uint y=0; y<imY; y++) for(uint x=0; x<imX; x++) target(x,y) = uint(source[y*X+x]) * 0xFF / volume.maximum;
        }
    }
    else if(volume.sampleSize==4) {
        if(offsetX || offsetY || offsetZ) {
            const uint32* const sourceZ = (const Volume32&)volume + offsetZ[z];
            for(uint y=0; y<imY; y++) {
                const uint32* const sourceY = sourceZ + offsetY[y];
                for(uint x=0; x<imX; x++) target(x,y) = uint(sourceY[offsetX[x]]) * 0xFF / volume.maximum;
            }
        } else {
            const uint32* const source = (const Volume32&)volume + z*Y*X + mY*X + mX;
            for(uint y=0; y<imY; y++) for(uint x=0; x<imX; x++) target(x,y) = uint(source[y*X+x]) * 0xFF / volume.maximum;
        }
    } else error("Unsupported sample size", volume.sampleSize);
    return target;
}

Image squareRoot(const Volume& volume, uint z) {
    uint X=volume.x, Y=volume.y;
    uint mX=volume.marginX, mY=volume.marginY;
    uint imX=X-2*mX, imY=Y-2*mY;
    Image target(imX,imY);
    const uint* const offsetX = volume.offsetX;
    const uint* const offsetY = volume.offsetY;
    const uint* const offsetZ = volume.offsetZ;
    float scale = 0xFF / round(sqrt(volume.maximum));
    if(volume.sampleSize==2) {
        if(offsetX || offsetY || offsetZ) {
            const uint16* const sourceZ = (const Volume16&)volume + offsetZ[z];
            for(uint y=0; y<imY; y++) {
                const uint16* const sourceY = sourceZ + offsetY[mY+y];
                for(uint x=0; x<imX; x++) target(x,y) = uint8(min<float>(0xFF,sqrt(float(sourceY[offsetX[mX+x]])) * scale));
            }
        } else {
            const uint16* const sourceZ = (const Volume16&)volume + z*Y*X + mY*X + mX;
            for(uint y=0; y<imY; y++) {
                const uint16* const sourceY = sourceZ + y*X;
                for(uint x=0; x<imX; x++) target(x,y) = uint8(min<float>(0xFF,sqrt(float(sourceY[x])) * scale));
            }
        }
    } else if(volume.sampleSize==4) {
        if(offsetX || offsetY || offsetZ) {
            const uint32* const sourceZ = (const Volume32&)volume + offsetZ[z];
            for(uint y=0; y<imY; y++) {
                const uint32* const sourceY = sourceZ + offsetY[mY+y];
                for(uint x=0; x<imX; x++) target(x,y) = uint8(min<float>(0xFF,sqrt(float(sourceY[offsetX[mX+x]])) * scale));
            }
        } else {
            const uint32* const sourceZ = (const Volume32&)volume + z*Y*X + mY*X + mX;
            for(uint y=0; y<imY; y++) {
                const uint32* const sourceY = sourceZ + y*X;
                for(uint x=0; x<imX; x++) target(x,y) = uint8(min<float>(0xFF,sqrt(float(sourceY[x])) * scale)); //FIXME: overflow
            }
        }
    } else error("Unsupported sample size", volume.sampleSize);
    return target;
}
