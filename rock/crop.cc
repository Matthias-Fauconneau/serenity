#include "crop.h"
#include "volume-operation.h"
#include "sample.h"

String str(const CropVolume& crop) { return "("_+str(crop.min, crop.max)+")"_; }

Cylinder parseCylinder(string cylinder, int3 center, int3 extra) {
    int3 min,max;
    if(cylinder.contains(',')) { // x, y, r, zMin, zMax
        Vector coordinates = parseVector(cylinder, true);
        int x=coordinates[0], y=coordinates[1], r=coordinates[2]; min.z=coordinates[3], max.z=coordinates[4];
        min.x=x-r, min.y=y-r, max.x=x+r, max.y=y+r;
    } else { // Crops centered cylinder
        int r = toInteger(cylinder);
        min=center-int3(r), max=center+int3(r);
    }
    min -= extra, max += extra; // Adds extra voxels to user-specified geometry to compensate margins lost to process
    return {min, max};
}

CropVolume alignCrop(int3 min, int3 max, int3 sourceMargin, int3 sourceMin, int3 sourceMax, int3 minimalMargin) {
    assert_(int(max.x-min.x) == int(max.y-min.y), min, max);
    min += sourceMargin, max += sourceMargin;
    if((max.x-min.x)%2) { if(max.x%2) max.x--; else assert(min.x%2), min.x++; }
    if((max.y-min.y)%2) { if(max.y%2) max.y--; else assert(min.y%2), min.y++; }
    if((max.z-min.z)%2) { if(max.z%2) max.z--; else assert(min.z%2), min.z++; }
    int3 size = max-min;
    // Asserts valid volume
    assert_(min>=sourceMin && min<max && max<=sourceMax, sourceMin, min, max, sourceMax);
    assert_( size.x%2 == 0 && size.y%2 == 0 && size.z%2 == 0); // Margins are currently always symmetric (i.e even volume size)
    assert_(size.x == size.y);
    // Align sampleCount for correct tiled indexing
    int3 sampleCount = int3(nextPowerOfTwo(size.x), nextPowerOfTwo(size.y), nextPowerOfTwo(size.z));
    int3 margin = ::max(minimalMargin, (sampleCount - size)/2);
    sampleCount = int3(nextPowerOfTwo(size.x+2*margin.x), nextPowerOfTwo(size.y+2*margin.y), nextPowerOfTwo(size.z+2*margin.z));
    while(sampleCount.y < sampleCount.z   ) sampleCount.y*=2; // Z-order: [Z Y] X z y x
    while(sampleCount.x < sampleCount.y   ) sampleCount.x*=2; // Z-order: Z [Y X] z y x
    while(sampleCount.z < sampleCount.x   ) sampleCount.z*=2; // Z-order: Z Y [X z] y x
    while(sampleCount.y < sampleCount.z/2) sampleCount.y*=2; // Z-order: Z Y X [z y] x
    while(sampleCount.x < sampleCount.y/2) sampleCount.x*=2; // Z-order: Z Y X z [y x]
    margin = (sampleCount - size)/2;
    assert_( size+2*margin == sampleCount );
    assert_( margin >= minimalMargin );
    return {min, max, size, sampleCount, margin};
}

/// Parses volume to crop from user arguments
CropVolume parseCrop(const Dict& args, int3 sourceMargin, int3 sourceMin, int3 sourceMax, int3 extra, int3 minimalMargin) {
    bool downsample = args.value("downsample"_,"0"_)!="0"_;
    int3 min=sourceMin-(downsample?2:1)*sourceMargin, max=sourceMax-(downsample?2:1)*sourceMargin;
    if(args.contains("cylinder"_)) {
        Cylinder cylinder = parseCylinder(args.at("cylinder"_), (min+max)/2, extra);
        min = cylinder.min, max=cylinder.max;
    }
    if(downsample) min.x /=2, min.y /= 2, min.z /= 2, max.x /= 2, max.y /= 2, max.z /= 2;
    return alignCrop(min, max, sourceMargin, sourceMin, sourceMax, minimalMargin);
}

/// Parses volume to crop from user arguments (transforms global cooordinates to input coordinates using crop specifications of input volume)
CropVolume parseGlobalCropAndTransformToInput(const Dict& args, int3 margin, int3 min, int3 max) {
    if(args.contains("inputCylinder"_) && args.contains("cylinder"_)) { // Converts to input coordinates
        Cylinder input = parseCylinder(args.at("inputCylinder"_), (min+max)/2); // Parses input cylinder in global coordinates
        Cylinder global = parseCylinder(args.at("cylinder"_), (min+max)/2); // Parses crop cylinder in global coordinates
        log(input.min, input.max, args.at("inputCylinder"_), global.min, global.max, args.at("cylinder"_));
        return alignCrop(global.min-input.min, global.max-input.min, margin, min, max); // Transforms from global coordinates to input coordinates and aligns
    } else { // Local crop (using input coordinates)
        return parseCrop(args, margin, min, max); // Using input coordinates
    }
}

/// Crops a volume to remove boundary effects
void crop(Volume16& target, const Volume16& source, CropVolume crop) {
    assert_(source.tiled() && target.tiled());
    target.sampleCount = crop.sampleCount;
    assert_(crop.min>=source.margin && crop.max<=source.sampleCount-source.margin, source.margin, crop.min, crop.max, source.sampleCount-source.margin);
    target.margin = crop.margin;
    assert_(target.data.size >= target.size()*target.sampleSize);
    target.data.size = target.size()*target.sampleSize;
    const uint64 X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z;
    const uint marginX=target.margin.x, marginY=target.margin.y, marginZ=target.margin.z;
    const uint64* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    for(uint z=0; z<marginZ; z++) for(uint y=0; y<Y; y++) for(uint x=0; x<X; x++) targetData[offsetZ[z]+offsetY[y]+offsetX[x]]=0;
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint16* const sourceZ = sourceData + offsetZ[crop.min.z-marginZ+z];
        uint16* const targetZ = targetData + offsetZ[z];
        for(uint y=0; y<marginY; y++) for(uint x=0; x<X; x++) targetZ[offsetY[y]+offsetX[x]]=0;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* const sourceZY = sourceZ + offsetY[crop.min.y-marginY+y];
            uint16* const targetZY = targetZ + offsetY[y];
            for(uint x=0; x<marginX; x++) targetZY[offsetX[x]]=0;
            for(uint x=marginX; x<X-marginX; x++) targetZY[offsetX[x]]=sourceZY[offsetX[crop.min.x-marginX+x]];
            for(uint x=X-marginX; x<X; x++) targetZY[offsetX[x]]=0;
        }
        for(uint y=X-marginY; y<Y; y++) for(uint x=0; x<X; x++) targetZ[offsetY[y]+offsetX[x]]=0;
    }
    for(uint z=Z-marginZ; z<Z; z++) for(uint y=0; y<Y; y++) for(uint x=0; x<X; x++) targetData[offsetZ[z]+offsetY[y]+offsetX[x]]=0;
}
class(Crop,Operation), virtual VolumePass<uint16> {
    string parameters() const override { return "cylinder downsample inputCylinder inputDownsample"_; }
    void execute(const Dict& args, Volume16& target, const Volume& source) override {
        crop(target, source, parseGlobalCropAndTransformToInput(args, source.margin, source.margin, source.sampleCount-source.margin));
    }
};
