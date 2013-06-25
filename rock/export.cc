#include "volume-operation.h"
#include "sample.h"
#include "time.h"
#include "png.h"
#include "bmp.h"

/// Square roots all values
void squareRoot(VolumeFloat& target, const Volume16& source) {
    assert_(source.squared);
    target.maximum = ceil(sqrt((float)source.maximum)), target.squared=false;
    const uint16* const sourceData = source; float* const targetData = target;
    for(uint index: range(source.size())) targetData[index] = sqrt((float)sourceData[index]);
    target.floatingPoint = true;
}
defineVolumePass(SquareRoot, float, squareRoot);

/// Scales all values
void scaleValues(VolumeFloat& target, const VolumeFloat& source, const float scale) {
    assert_(source.floatingPoint);
    target.maximum = ceil(scale*source.maximum), target.squared=false;
    const float* const sourceData = source; float* const targetData = target;
    for(uint index: range(source.size())) targetData[index] = scale*sourceData[index];
}
class(ScaleValues, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(float); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        scaleValues(outputs[0], inputs[0], TextData(otherInputs[0]->data).decimal());
    }
};

/// Exports volume to normalized 8bit PNGs for visualization
class(ToPNG, Operation), virtual VolumeOperation {
    string parameters() const { return "cylinder"_; }
    void execute(const Dict& args, const mref<Volume>&, const ref<Volume>& inputs, const mref<Result*>& outputs) override {
        const Volume& volume = inputs[0];
        outputs[0]->metadata = String("png"_);
        uint marginZ = volume.margin.z;
        Time time; Time report;
        for(int z: range(marginZ, volume.sampleCount.z-marginZ)) {
            if(report/1000>=7) { log(z-marginZ,"/",volume.sampleCount.z-marginZ, ((z-marginZ)*volume.sampleCount.x*volume.sampleCount.y/1024/1024)/(time/1000), "MB/s"); report.reset(); }
            outputs[0]->elements.insert(dec(z,4), encodePNG(slice(volume,z,args.contains("cylinder"_))));
        }
    }
};

/// Exports volume to normalized 8bit BMPs for visualization
class(ToBMP, Operation), virtual VolumeOperation {
    string parameters() const { return "cylinder"_; }
    void execute(const Dict& args, const mref<Volume>&, const ref<Volume>& inputs, const mref<Result*>& outputs) override {
        const Volume& volume = inputs[0];
        outputs[0]->metadata = String("bmp"_);
        uint marginZ = volume.margin.z;
        Time time; Time report;
        for(int z: range(marginZ, volume.sampleCount.z-marginZ)) {
            if(report/1000>=7) { log(z-marginZ,"/",volume.sampleCount.z-marginZ, ((z-marginZ)*volume.sampleCount.x*volume.sampleCount.y/1024/1024)/(time/1000), "MB/s"); report.reset(); }
            outputs[0]->elements.insert(dec(z,4), encodeBMP(slice(volume,z,args.contains("cylinder"_))));
        }
    }
};

/// Converts integers to ASCII decimal
template<uint pad> inline void itoa(byte*& target, uint n) {
    int i = pad;
    do { target[--i]="0123456789"[n%10]; n /= 10; } while( n!=0 );
    while(i>0) target[--i]=' ';
    target[pad]=',';
    target += pad+1;
}

/// Exports volume to ASCII, one sample per line formatted as "x, y, z, f(x,y,z)"
buffer<byte> toASCII(const Volume& source) {
    uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    uint marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    buffer<byte> target (X*Y*Z*(3*5+6)); assert_(X<=1e4 && Y<=1e4 && Z<=1e4 && source.maximum < 1e5);
    byte *targetPtr = target.begin();
    for(uint z=marginZ; z<Z-marginZ; z++) {
        for(uint y=marginY; y<Y-marginY; y++) {
            for(uint x=marginX; x<X-marginX; x++) {
                uint index = source.tiled() ? offsetX[x] + offsetY[y] + offsetZ[z] : z*XY + y*X + x;
                uint value = 0;
                if(source.sampleSize==1) value = ((byte*)source.data.data)[index];
                else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
                else if(source.sampleSize==4 && !source.floatingPoint) value = ((uint32*)source.data.data)[index];
                else if(source.sampleSize==4 && source.floatingPoint) value = round(((float*)source.data.data)[index]); //FIXME: converts to ASCII with decimals
                else error(source.sampleSize);
                assert_(value <= source.maximum, value, source.maximum, source.sampleSize);
                if(value) { itoa<4>(targetPtr, x); itoa<4>(targetPtr, y); itoa<4>(targetPtr, z); itoa<5>(targetPtr, value); targetPtr[-1]='\n'; }
            }
        }
    }
    target.size = targetPtr-target.begin(); assert(target.size <= target.capacity);
    return target;
}
class(ToASCII, Operation), virtual VolumeOperation {
    void execute(const Dict&, const mref<Volume>&, const ref<Volume>& inputs, const mref<Result*>& outputs) override {
        outputs[0]->metadata = String("ascii"_);
        outputs[0]->data = toASCII(inputs[0]);
    }
};

FILE(CDL)
/// Exports volume to unidata netCDF CDL (network Common data form Description Language) (can be converted to a binary netCDF dataset using ncgen)
String toCDL(const Volume& source) {
    uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    uint marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    constexpr uint positionSize="8191"_.size; buffer<byte> positions (X*Y*Z*3*(positionSize+1)); assert_(X<=1e4 && Y<=1e4 && Z<=1e4);
    constexpr uint valueSize="65535"_.size; buffer<byte> values (X*Y*Z*(valueSize+1)); assert_(source.maximum < 1e5);
    byte *positionIndex = positions.begin(), *valueIndex = values.begin();
    for(uint z=marginZ; z<Z-marginZ; z++) {
        for(uint y=marginY; y<Y-marginY; y++) {
            for(uint x=marginX; x<X-marginX; x++) {
                uint index = source.tiled() ? offsetX[x] + offsetY[y] + offsetZ[z] : z*XY + y*X + x;
                uint value = 0;
                if(source.sampleSize==1) value = ((byte*)source.data.data)[index];
                else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
                else if(source.sampleSize==4 && !source.floatingPoint) value = ((uint32*)source.data.data)[index];
                else if(source.sampleSize==4 && source.floatingPoint) value = round(((float*)source.data.data)[index]); //FIXME: converts to ASCII with decimals
                else error(source.sampleSize);
                assert(value <= source.maximum, value, source.maximum);
                if(value) itoa<positionSize>(positionIndex, x), itoa<positionSize>(positionIndex, y), itoa<positionSize>(positionIndex, z), itoa<valueSize>(valueIndex, value);
            }
        }
    }
    positions.size = positionIndex-positions.begin(); assert(positions.size <= positions.capacity);
    values.size = valueIndex-values.begin(); assert(values.size <= values.capacity);
    uint valueCount = values.size / (valueSize+1);
    string header = CDL();
    String data (header.size + 3*valueCount*"0,"_.size + positions.size + values.size);
    for(TextData s(header);;) {
        data << s.until('$'); // Copies header until next substitution
        /***/ if(s.match('#')) data << str(valueCount); // Substitutes non-zero values count
        else if(s.match('0')) { data << repeat("0,"_, valueCount), data.last()=';'; } // Substitutes zeroes
        else if(s.match('1')) { data << repeat("1,"_, valueCount); data.last()=';'; } // Substitutes ones
        else if(s.match("position"_)) { data << positions; data.last()=';'; } // Substitutes sample positions
        else if(s.match("value"_)) { data << values; data.last()=';'; } // Substitutes sample values
        else if(!s) break;
        else error("Unknown substitution",s.until(';'));
    }
    return data;
}
class(ToCDL, Operation), virtual VolumeOperation {
    void execute(const Dict&, const mref<Volume>&, const ref<Volume>& inputs, const mref<Result*>& outputs) override {
        outputs[0]->metadata = String("cdl"_);
        outputs[0]->data = toCDL(inputs[0]);
    }
};
