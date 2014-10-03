#include "crop.h"
#include "volume-operation.h"
#include "sample.h"

String str(const CropVolume& crop) { return "("_+str(crop.min, crop.max)+")"_; }

/// Parses volume to crop from user arguments
CropVolume parseCrop(const Dict& args, int3 sourceMin, int3 sourceMax, int3 origin, int3 extra) {
    int3 min=sourceMin, max=sourceMax, center=(min+max)/2;
    /**/ if(args.contains("cylinder"_)) {
        assert_(!args.value("box"_,""_));
        Vector coordinates = parseVector(args.at("cylinder"_), true);
        if(args.value("downsample"_,"0"_)!="0"_) coordinates = 1./2 * coordinates; // User inputs full size coordinates
        if(coordinates.size==1) { // Crops centered cylinder
            int r = coordinates[0];
            min = center - int3(r), max = center + int3(r);
        } else if(coordinates.size==2) { // Crops centered cylinder
            int r = coordinates[0], z=coordinates[1];
            min = center - int3(r,r,z/2), max = center + int3(r,r,z/2);
        } else if(coordinates.size==5) { // x,y,r,z0,z1
            int x = coordinates[0], y = coordinates[1], r = coordinates[2];
            min = origin + int3(x-r, y-r, coordinates[3]);
            max = origin + int3(x+r, y+r, coordinates[4]);
        } else error("Expected cylinder = r | x,y,r,z0,z1, got", args.at("cylinder"_));
        min -= extra, max += extra; // Adds extra voxels to user-specified geometry to compensate margins lost to process
    }
    else if(args.value("box"_,""_)) {
        Vector coordinates = parseVector(args.at("box"_), true);
        if(args.value("downsample"_,"0"_)!="0"_) coordinates = 1./2 * coordinates; // User inputs full size coordinates
        if(coordinates.size==1) { // Crops centered box
            int3 size = coordinates[0];
            min = center - size/2, max = center + size/2;
        } else if(coordinates.size==2) { // Crops centered box
            int r = coordinates[0], z = coordinates[1];
            min = center - int3(r,r,z/2), max = center + int3(r,r,z/2);
        } else if(coordinates.size==3) { // Crops centered box
            int3 size = int3(coordinates[0],coordinates[1],coordinates[2]);
            min = center-size/2, max = center+size/2;
        } else if(coordinates.size==6) {
            min = origin + int3(coordinates[0],coordinates[1],coordinates[2]);
            max = origin + int3(coordinates[3],coordinates[4],coordinates[5]);
        } else error("Expected box = size | size{x,y,z} | x0,y0,z0,x1,y1,z1, got", args.at("box"_));
        min -= extra, max += extra; // Adds extra voxels to user-specified geometry to compensate margins lost to process
    }
    else {
        if(args.value("downsample"_,"0"_)!="0"_) {
            min.x = align(2,min.x)/2, max.x /= 2;
            min.y = align(2,min.y)/2, max.y /= 2;
            min.z = align(2,min.z)/2, max.z /= 2;
            sourceMin.x = align(2,min.x)/2, sourceMax.x /= 2;
            sourceMin.y = align(2,min.y)/2, sourceMax.y /= 2;
            sourceMin.z = align(2,min.z)/2, sourceMax.z /= 2;
        }
    }

    assert_(int(max.x-min.x) == int(max.y-min.y), min, max);
    if((max.x-min.x)%2) { if(max.x%2) max.x--; else assert(min.x%2), min.x++; }
    if((max.y-min.y)%2) { if(max.y%2) max.y--; else assert(min.y%2), min.y++; }
    if((max.z-min.z)%2) { if(max.z%2) max.z--; else assert(min.z%2), min.z++; }
    int3 size = max-min;
    // Asserts valid volume
    assert_( size.x%2 == 0 && size.y%2 == 0 && size.z%2 == 0); // Margins are currently always symmetric (i.e even volume size)
    assert_(size.x == size.y);
    // Align sampleCount for correct tiled indexing
    int3 sampleCount = nextPowerOfTwo(size);
    int3 margin = (sampleCount - size)/2;
    assert_(sampleCount == int3(nextPowerOfTwo(size.x+2*margin.x), nextPowerOfTwo(size.y+2*margin.y), nextPowerOfTwo(size.z+2*margin.z)));
    assert_(sampleCount>int3(1), sampleCount);
    while(sampleCount.y < sampleCount.z   ) sampleCount.y*=2; // Z-order: [Z Y] X z y x
    while(sampleCount.x < sampleCount.y   ) sampleCount.x*=2; // Z-order: Z [Y X] z y x
    while(sampleCount.z < sampleCount.x/2) sampleCount.z*=2; // Z-order: Z Y [X z] y x
    while(sampleCount.y < sampleCount.z/2) sampleCount.y*=2; // Z-order: Z Y X [z y] x
    while(sampleCount.x < sampleCount.y/2) sampleCount.x*=2; // Z-order: Z Y X z [y x]
    margin = (sampleCount - size)/2;
    assert_( size+2*margin == sampleCount );
    assert_(int3(0)<=sourceMin, "source min:", sourceMin);
    assert_(sourceMin<=min, "source min:", sourceMin, "crop min:", min, "crop max:", max, "source max:", sourceMax, args, origin, extra);
    assert_(min<max, "crop min:", min, "crop max:", max);
    assert_(max<=sourceMax, "crop max:", max, "source max:", sourceMax, args, origin, extra, sourceMin);
    return {min, max, size, sampleCount, margin, origin + margin - min, args.contains("cylinder"_)};
}

/// Crops a volume to remove boundary effects
void crop(Volume16& target, const Volume16& source, CropVolume crop) {
    bool tiled = source.tiled();
    interleavedLookup(target);
    target.sampleCount = crop.sampleCount;
    assert_(crop.min>=source.margin && crop.max<=source.sampleCount-source.margin, "margin", source.margin, "min", crop.min, "max", crop.max, "size", source.sampleCount-source.margin);
    target.margin = crop.margin;
    target.origin = crop.origin;
    target.cylinder = crop.cylinder;
    assert_(target.data.size >= target.size()*target.sampleSize);
    target.data.size = target.size()*target.sampleSize;
    const uint64 X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z;
    const uint marginX=target.margin.x, marginY=target.margin.y, marginZ=target.margin.z;
    const ref<uint64> offsetX = target.offsetX, offsetY = target.offsetY, offsetZ = target.offsetZ;
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    for(uint z=0; z<marginZ; z++) for(uint y=0; y<Y; y++) for(uint x=0; x<X; x++) targetData[offsetZ[z]+offsetY[y]+offsetX[x]]=0;
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint16* const sourceZ = sourceData + (tiled ? offsetZ[crop.min.z-marginZ+z] : ((crop.min.z-marginZ+z)*Y*X));
        uint16* const targetZ = targetData + offsetZ[z];
        for(uint y=0; y<marginY; y++) for(uint x=0; x<X; x++) targetZ[offsetY[y]+offsetX[x]]=0;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* const sourceZY = sourceZ + (tiled ? offsetY[crop.min.y-marginY+y] : ((crop.min.y-marginY+y)*X));
            uint16* const targetZY = targetZ + offsetY[y];
            for(uint x=0; x<marginX; x++) targetZY[offsetX[x]]=0;
            for(uint x=marginX; x<X-marginX; x++) targetZY[offsetX[x]]=sourceZY[tiled ? offsetX[crop.min.x-marginX+x] : (crop.min.x-marginX+x)];
            for(uint x=X-marginX; x<X; x++) targetZY[offsetX[x]]=0;
        }
        for(uint y=X-marginY; y<Y; y++) for(uint x=0; x<X; x++) targetZ[offsetY[y]+offsetX[x]]=0;
    }
    for(uint z=Z-marginZ; z<Z; z++) for(uint y=0; y<Y; y++) for(uint x=0; x<X; x++) targetData[offsetZ[z]+offsetY[y]+offsetX[x]]=0;
}

struct Crop : VolumePass<uint16> {
    string parameters() const override { return "cylinder box downsample"_; }
    void execute(const Dict& args, Volume16& target, const Volume& source) override {
        CropVolume crop = parseCrop(args, source.margin, source.sampleCount-source.margin, source.origin);
        ::crop(target, source, crop);
    }
};
template struct Interface<Operation>::Factory<Crop>;
