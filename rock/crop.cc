#include "crop.h"
#include "volume-operation.h"
#include "sample.h"

/// Parses volume to crop from user arguments
CropVolume parseCrop(const Dict& args, int3 sourceMin, int3 sourceMax, string box, int3 minimalMargin) {
    int3 min=sourceMin, max=sourceMax, center=(min+max)/2;
    bool cylinder = args.contains("cylinder"_);
    if(cylinder) {
        if(args.at("cylinder"_)!=""_) {
            if(args.at("cylinder"_).contains(',')) { // x, y, r, zMin, zMax
                Vector<int> coordinates = parseVector<int>(args.at("cylinder"_));
                int x=coordinates[0], y=coordinates[1], r=coordinates[2]; min.z=coordinates[3], max.z=coordinates[4];
                min.x=x-r, min.y=y-r, max.x=x+r, max.y=y+r;
            } else { // Crops centered cylinder
                int r = toInteger(args.at("cylinder"_));
                min=center-int3(r), max=center+int3(r);
            }
        }
        int margin = int(max.x-min.x) - int(max.y-min.y);
        if(margin > 0) min.x+=margin/2, max.x-=margin/2;
        if(margin < 0) min.y+=(-margin)/2, max.y-=(-margin)/2;
    }
    if(args.contains("box"_) && args.at("box"_)!="auto"_) box = args.at("box"_); // "box" argument overrides input
    if(box) {
        if(box.contains(',')) {
            Vector<int> coordinates = parseVector<int>(box);
            if(coordinates.size == 6) min=int3(coordinates[0], coordinates[1], coordinates[2]), max=int3(coordinates[3],coordinates[4],coordinates[5]); // Generic box
            else if(coordinates.size == 3) { int3 size (coordinates[0],coordinates[1],coordinates[2]); min=max/2-size/2, max=max/2+size/2; } // Crops centered box
        } else { int size = TextData(box).integer(); min=center-int3(size/2), max=center+int3(size/2); } // Crops centered cube
    }
    if(args.value("downsample"_,"0"_)!="0"_) min.x /=2, min.y /= 2, min.z /= 2, max.x /= 2, max.y /= 2, max.z /= 2;
    if((max.x-min.x)%2) { if(max.x%2) max.x--; else assert(min.x%2), min.x++; }
    if((max.y-min.y)%2) { if(max.y%2) max.y--; else assert(min.y%2), min.y++; }
    if((max.z-min.z)%2) { if(max.z%2) max.z--; else assert(min.z%2), min.z++; }
    int3 size = max-min;
    // Asserts valid volume
    assert_(min>=sourceMin && min<max && max<=sourceMax, sourceMin, min, max, sourceMax);
    assert_( size.x%2 == 0 && size.y%2 == 0 && size.z%2 == 0); // Margins are currently always symmetric (i.e even volume size)
    if(cylinder) assert_(size.x == size.y);
    // Align sampleCount for correct tiled indexing
    int3 sampleCount = int3(nextPowerOfTwo(size.x), nextPowerOfTwo(size.y), nextPowerOfTwo(size.z));
    int3 margin = ::max(minimalMargin, (sampleCount - size)/2);
    sampleCount = int3(nextPowerOfTwo(size.x+2*margin.x), nextPowerOfTwo(size.y+2*margin.y), nextPowerOfTwo(size.z+2*margin.z));
    while(sampleCount.x < ::min(sampleCount.y, sampleCount.z)/2) sampleCount.x*=2;
    while(sampleCount.y < ::min(sampleCount.z, sampleCount.x)/2) sampleCount.y*=2;
    while(sampleCount.z < ::min(sampleCount.x, sampleCount.y)/2) sampleCount.z*=2;
    margin = (sampleCount - size)/2;
    assert_( size+2*margin == sampleCount );
    assert_( margin >= minimalMargin );
    return {min, max, size, sampleCount, margin, cylinder};
}

/// Crops a volume to remove boundary effects
void crop(Volume16& target, const Volume16& source, CropVolume crop) {
    assert_(source.tiled() && target.tiled());
    target.sampleCount = crop.sampleCount;
    assert_(crop.min>=source.margin && crop.max<=source.sampleCount-source.margin, crop.min, crop.max, source.margin, source.sampleCount);
    target.margin = crop.margin;
    assert_(target.data.size >= target.size()*target.sampleSize);
    target.data.size = target.size()*target.sampleSize;
    const uint X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z;
    const uint marginX=target.margin.x, marginY=target.margin.y, marginZ=target.margin.z;
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
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
    string parameters() const override { static auto p="cylinder box"_; return p; }
    void execute(const Dict& args, Volume16& target, const Volume& source) override { crop(target, source, parseCrop(args, source.margin, source.sampleCount-source.margin, ""_, 1)); }
};
