#include "volume-operation.h"
#include "sample.h"
#include "time.h"
#include "png.h"
#include "bmp.h"
#include "tiff.h"

/// Negates volume (when source = 0, target is 1; when source > 0, target = 0)
static void negate(Volume8& target, const Volume8& source) {
    const uint8* const sourceData = source; uint8* const targetData = target;
    for(uint index: range(source.size())) targetData[index] = !sourceData[index];
    target.maximum=1;
}
defineVolumePass(Negate, uint8, negate);

/// Adds two volumes
static void add(Volume8& target, const Volume8& A, const Volume8& B) {
    const uint8* const aData = A; const uint8* const bData = B; uint8* const targetData = target;
    for(uint index: range(target.size())) targetData[index] = aData[index] + bData[index];
    target.maximum= A.maximum + B.maximum;
}
class(Add, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint8); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        add(outputs[0], inputs[0], inputs[1]);
    }
};

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
static void scaleValues(VolumeFloat& target, const VolumeFloat& source, const float scale) {
    assert_(source.floatingPoint);
    target.maximum = ceil(scale*source.maximum), target.squared=false;
    const float* const sourceData = source; float* const targetData = target;
    for(uint index: range(source.size())) targetData[index] = scale*sourceData[index];
}
class(ScaleValues, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(float); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        scaleValues(outputs[0], inputs[0], parseScalar(otherInputs[0]->data));
    }
};

/// Normalizes to 8bit
void normalize8(Volume8& target, const Volume& source) {
    uint8* const targetData = target;
    uint minimum = source.sampleSize==2 ? ::minimum(source) : 0;
    uint maximum = source.maximum;
    if(source.squared) minimum=round(sqrt(real(minimum))), maximum=round(sqrt(real(maximum)));
    assert_(maximum>minimum);
    for(uint index: range(source.size())) {
        int value;
        if(source.sampleSize==1) value = ((uint8*)source.data.data)[index];
        else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
        else error("");
        int v = source.squared ?
                    min<int>(0xFF,round((sqrt(real(value))-minimum)*0xFF/(maximum-minimum))) :
                    int(value-minimum)*0xFF/(maximum-minimum);
        assert_(v>=0 && v<=0xFF, v);
        targetData[index] = v;
    }
    target.squared=false;
    target.maximum = 0xFF;
}
defineVolumePass(Normalize8, uint8, normalize8);

/// Clips voxels to maximum
static void maximum(Volume16& target, const Volume16& source, uint16 maximum) {
    const uint16* const sourceData = source; uint16* const targetData = target;
    for(uint index: range(source.size())) targetData[index] = min(sourceData[index], maximum);
    target.maximum = min<uint>(source.maximum, maximum);
}
class(Maximum, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        maximum(outputs[0], inputs[0], parseScalar(otherInputs[0]->data)*inputs[0].maximum);
    }
};

/// Clips voxels to minimum
static void minimum(Volume16& target, const Volume16& source, uint16 minimum) {
    const uint16* const sourceData = source; uint16* const targetData = target;
    for(uint index: range(source.size())) targetData[index] = max(sourceData[index], minimum);
}
class(Minimum, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        minimum(outputs[0], inputs[0], parseScalar(otherInputs[0]->data)*inputs[0].maximum);
    }
};

/// Sets masked (mask=0) voxels where source is under/over masked value to masked value
static void mask(Volume16& target, const Volume16& source, const Volume8& mask, uint16 value, bool invert) {
    assert_(source.size()==mask.size() && target.size() == source.size() && target.tiled() && source.tiled() && mask.tiled(), source, mask, target, source.sampleCount-2*source.margin, mask.sampleCount-2*mask.margin, target.sampleCount-2*target.margin);
    target.margin = mask.margin;
    const uint8* const maskData = mask; const uint16* const sourceData = source; uint16* const targetData = target;
    if(invert) for(uint index: range(source.size())) { uint16 s=sourceData[index]; targetData[index] = maskData[index] || s<value ? s: value; }
    else for(uint index: range(source.size())) { uint16 s=sourceData[index]; targetData[index] = maskData[index] || s>value ? s: value; }
}
class(Mask, Operation), virtual VolumeOperation {
    virtual string parameters() const { return "value invert"_; }
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        assert_(args.contains("value"_),"Missing mandatory argument 'value' for mask");
        float value = toDecimal(args.value("value"_,"0"_));
        uint16 integerValue = value <= 1 ? round( value*inputs[0].maximum ) : round(value);
        mask(outputs[0], inputs[0], inputs[1], integerValue, args.value("invert"_,"0"_)!="0"_);
    }
};

/// Maps intensity to either blue or green channel depending on binary classification
void colorize(Volume24& target, const Volume16& source, uint16 threshold) {
    const uint maximum = source.maximum;
    chunk_parallel(source.size(), [&](uint, uint offset, uint size) {
        const uint16* const sourceData = source + offset;
        bgr* const targetData = target + offset;
        for(uint i : range(size)) {
            uint8 c = 0xFF*sourceData[i]/maximum;
            targetData[i] = sourceData[i]<threshold ? bgr{0,c,0} : bgr{c,0,0};
        }
    });
    target.maximum=0xFF;
}
class(Colorize, Operation), virtual VolumeOperation {
    string parameters() const override { return "threshold"_; }
    uint outputSampleSize(uint) override { return sizeof(bgr); }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        real threshold = TextData( (args.contains("threshold"_) && isDecimal(args.at("threshold"_))) ? (string)args.at("threshold"_) : otherInputs[0]->data ).decimal();
        uint16 integerThreshold = threshold<1 ? round( threshold*inputs[0].maximum ) : round(threshold);
        colorize(outputs[0], inputs[0], integerThreshold);
    }
};

/// Exports volume to 8bit PNGs for visualization (normalized and gamma corrected)
class(ToPNG, Operation), virtual VolumeOperation {
    virtual string parameters() const { return "z invert binary"_; }
    void execute(const Dict& args, const mref<Volume>&, const ref<Volume>& inputs, const mref<Result*>& outputs) override {
        const Volume& volume = inputs[0];
        outputs[0]->metadata = String("png"_);
        if(args.contains("z"_)) {
            outputs[0]->data = encodePNG(slice(volume,(real)args.at("z"_),true, true, args.contains("invert"_), args.contains("binary"_)));
        } else {
            uint marginZ = volume.margin.z;
            Time time; Time report;
            for(int z: range(marginZ, volume.sampleCount.z-marginZ)) {
                if(report/1000>=7) { log(z-marginZ,"/",volume.sampleCount.z-marginZ, ((z-marginZ)*volume.sampleCount.x*volume.sampleCount.y/1024/1024)/(time/1000), "MB/s"); report.reset(); }
                outputs[0]->elements.insert(dec(z,4), encodePNG(slice(volume,z,true, true, args.contains("invert"_), args.contains("binary"_))));
            }
        }
    }
};

/// Exports volume to 8bit BMPs for interoperation (deprecated: use ToTIFF instead)
class(ToBMP, Operation), virtual VolumeOperation {
    void execute(const Dict&, const mref<Volume>&, const ref<Volume>& inputs, const mref<Result*>& outputs) override {
        const Volume& volume = inputs[0];
        outputs[0]->metadata = String("bmp"_);
        uint marginZ = volume.margin.z;
        Time time; Time report;
        for(int z: range(marginZ, volume.sampleCount.z-marginZ)) {
            if(report/1000>=7) { log(z-marginZ,"/",volume.sampleCount.z-marginZ, ((z-marginZ)*volume.sampleCount.x*volume.sampleCount.y/1024/1024)/(time/1000), "MB/s"); report.reset(); }
            outputs[0]->elements.insert(dec(z-marginZ,4), encodeBMP(slice(volume,z,false, false)));
        }
    }
};

/// Exports volume to 16bit TIFFs  for interoperation
class(ToTIFF, Operation), virtual VolumeOperation {
    void execute(const Dict&, const mref<Volume>&, const ref<Volume>& inputs, const mref<Result*>& outputs) override {
        const Volume& volume = inputs[0];
        outputs[0]->metadata = String("tiff"_);
        uint marginZ = volume.margin.z;
        Time time; Time report;
        for(int z: range(marginZ, volume.sampleCount.z-marginZ)) {
            if(report/1000>=7) { log(z-marginZ,"/",volume.sampleCount.z-marginZ, ((z-marginZ)*volume.sampleCount.x*volume.sampleCount.y/1024/1024)/(time/1000), "MB/s"); report.reset(); }
            assert_(!volume.tiled());
            outputs[0]->elements.insert(dec(z-marginZ,4), encodeTIFF(slice(volume, z)));
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
static buffer<byte> toASCII(const Volume& source) {
    const uint64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    const uint marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    const uint64* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
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
static String toCDL(const Volume& source) {
    uint64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    const uint marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    const uint64* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
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
    size_t outputSize(const Dict&, const ref<Result*>& inputs, uint) override {
        assert_(inputs);
        assert_(toVolume(*inputs[0]), inputs[0]->name, inputs[0]->metadata, inputs[0]->data.size);
        return toVolume(*inputs[0]).size() * 16;
    }
    void execute(const Dict&, const mref<Volume>&, const ref<Volume>& inputs, const mref<Result*>& outputs) override {
        outputs[0]->metadata = String("cdl"_);
        outputs[0]->data = toCDL(inputs[0]);
    }
};
