#include "volume-operation.h"
#include "sample.h"
#include "time.h"
#include "png.h"

/// Explicitly clips volume to cylinder by zeroing exterior samples
void cylinderClip(Volume& target, const Volume& source) {
    int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    uint radiusSq = (X/2-marginX)*(Y/2-marginY);
    assert_(!target.offsetX && !target.offsetY && !target.offsetZ);
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    uint16* const targetData = (uint16*)target.data.data;
    for(int z=0; z<Z; z++) {
        uint16* const targetZ = targetData + z*XY;
        for(int y=0; y<Y; y++) {
            uint16* const targetZY = targetZ + y*X;
            for(int x=0; x<X; x++) {
                uint value = 0;
                if(uint((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2)) <= radiusSq && z >= marginZ && z<Z-marginZ) {
                    uint index = offsetX ? offsetX[x] + offsetY[y] + offsetZ[z] : z*XY + y*X + x;
                    if(source.sampleSize==1) value = ((byte*)source.data.data)[index];
                    else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
                    else if(source.sampleSize==4) value = ((uint32*)source.data.data)[index];
                    else error(source.sampleSize);
                    assert(value <= source.maximum, value, source.maximum);
                }

                targetZY[x] = value;
            }
        }
    }
}
defineVolumePass(CylinderClip, uint16, cylinderClip);

/// Exports volume to normalized 8bit PNGs for visualization
class(ToPNG, Operation), virtual VolumeOperation {
    ref<byte> parameters() const { return "cylinder"_; }
    void execute(const Dict& args, const mref<Volume>&, const ref<Volume>& inputs, const mref<Result*>& outputs) override {
        const Volume& volume = inputs[0];
        outputs[0]->metadata = string("png"_);
        array<buffer<byte>>& elements = outputs[0]->elements;
        uint marginZ = volume.margin.z;
        Time time; Time report;
        for(int z: range(marginZ, volume.sampleCount.z-marginZ)) {
            if(report/1000>=7) { log(z-marginZ,"/",volume.sampleCount.z-marginZ, ((z-marginZ)*volume.sampleCount.x*volume.sampleCount.y/1024/1024)/(time/1000), "MB/s"); report.reset(); }
            elements << encodePNG(slice(volume,z,args.contains("cylinder"_)));
        }
    }
};

// Converts integers to ASCII decimal
template<uint pad> inline void itoa(byte*& target, uint n) {
    assert(n<1000);
    uint i=pad;
    do { target[--i]="0123456789"[n%10]; n /= 10; } while( n!=0 );
    assert(i>=1);
    while(i>1) target[--i]=' ';
    target += pad;
    target[0]=',';
}

FILE(CDL)
/// Exports volume to unidata netCDF CDL (network Common data form Description Language) (can be converted to a binary netCDF dataset using ncgen)
string toCDL(const Volume& source) {
    uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    uint marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    buffer<byte> positions (X*Y*Z*3*5); assert_(X<=1e4 && Y<=1e4 && Z<=1e4);
    buffer<byte> values (X*Y*Z*6); assert_(source.maximum < 1e5);
    byte *positionPtr = positions.begin(), *valuePtr = values.begin();
    for(uint z=marginZ; z<Z-marginZ; z++) {
        for(uint y=marginY; y<Y-marginY; y++) {
            for(uint x=marginX; x<X-marginX; x++) {
                uint index = offsetX ? offsetX[x] + offsetY[y] + offsetZ[z] : z*XY + y*X + x;
                uint value = 0;
                if(source.sampleSize==1) value = ((byte*)source.data.data)[index];
                else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
                else if(source.sampleSize==4) value = ((uint32*)source.data.data)[index];
                else error(source.sampleSize);
                assert(value <= source.maximum, value, source.maximum);
                if(value) itoa<5>(positionPtr, x), itoa<5>(positionPtr, y), itoa<5>(positionPtr, z), itoa<6>(valuePtr, value);
            }
        }
    }
    positions.size = positionPtr-positions.begin(); assert(positions.size <= positions.capacity);
    values.size = valuePtr-values.begin(); assert(values.size <= values.capacity);
    uint valueCount = values.size / 6;
    ref<byte> header = CDL();
    string data (header.size + 3*valueCount*2 + positions.size + values.size);
    for(TextData s(header);;) {
        data << s.until('$'); // Copies header until next substitution
        /***/ if(s.match('#')) data << str(valueCount); // Substitutes non-zero values count
        else if(s.match('0')) data << repeat("0,"_, valueCount), data.last()=';'; // Substitutes zeroes
        else if(s.match('1')) data << repeat("1,"_, valueCount), data.last()=';'; // Substitutes ones
        else if(s.match("position"_)) data << positions, data.last()=';'; // Substitutes sample positions
        else if(s.match("value"_)) data << values, data.last()=';'; // Substitutes sample values
        else if(!s) break;
        else error("Unknown substitution",s.until(';'));
    }
    return data;
}
class(ToCDL, Operation), virtual VolumeOperation {
    void execute(const Dict&, const mref<Volume>&, const ref<Volume>& inputs, const mref<Result*>& outputs) override {
        outputs[0]->metadata = string("cdl"_);
        outputs[0]->data = toCDL(inputs[0]);
    }
};
