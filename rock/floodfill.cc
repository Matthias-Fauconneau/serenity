#include "volume-operation.h"
#include "thread.h"
#include "time.h"

/// Transposes a volume permuting its coordinates
void transpose(Volume16& target, const Volume16& source) {
    const uint sX=source.sampleCount.x, sY=source.sampleCount.y, sZ=source.sampleCount.z;
    const uint tX=sY, tY=sZ;
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    for(uint z: range(sZ)) for(uint y: range(sY)) for(uint x: range(sX)) targetData[x*tX*tY + z*tX + y] = sourceData[z*sX*sY + y*sX + x];
}
defineVolumePass(Transpose, uint16, transpose);

/// Clips volume to values above a thresold
void thresholdClip(Volume16& target, const Volume16& source, uint threshold) {
    chunk_parallel(source.size(), [&](uint, uint offset, uint size) {
        const uint16* const sourceData = source + offset;
        uint16* const targetData = target + offset;
        for(uint i : range(size)) targetData[i] = sourceData[i] > threshold ? sourceData[i] : 0;
    });
}
class(ThresholdClip, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        uint clipThreshold = TextData(otherInputs[0]->data).integer();
        thresholdClip(outputs[0], inputs[0], clipThreshold);
    }
};

void floodFill(Volume8& target, const Volume8& source, string seed="111111"_, uint margin=0) {
    assert_(source.tiled() && target.tiled());
    const uint64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const uint marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    const uint8* const sourceData = source;

    buffer<short3> stackBuffer( X*Y*Z ); // 1024^3 ~ 6GiB
    short3* const stack = stackBuffer.begin();
    uint64 stackSize=0;

    // Seeds faces
    if(seed[0]=='1') for(uint z=marginZ;z<Z-marginZ;z++) for(uint y=marginY;y<Y-marginY;y++) stack[stackSize++] = short3(marginX+margin,y,z);
    if(seed[1]=='1') for(uint z=marginZ;z<Z-marginZ;z++) for(uint x=marginX;x<X-marginX;x++) stack[stackSize++] = short3(x,marginY+margin,z);
    if(seed[2]=='1') for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) stack[stackSize++] = short3(x,y,marginZ+margin);
    if(seed[3]=='1') for(uint z=marginZ;z<Z-marginZ;z++) for(uint y=marginY;y<Y-marginY;y++) stack[stackSize++] = short3(X-1-marginX-margin,y,z);
    if(seed[4]=='1') for(uint z=marginZ;z<Z-marginZ;z++) for(uint x=marginX;x<X-marginX;x++) stack[stackSize++] = short3(x,Y-1-marginY-margin,z);
    if(seed[5]=='1') for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) stack[stackSize++] = short3(x,y,Z-1-marginZ-margin);

    uint8* const targetData = target;
    clear(targetData, target.size());
    const uint64* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;

    while(stackSize) {
        const short3& p = stack[--stackSize];
        uint x=p.x, y=p.y, z=p.z;
        for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) { // 26-way connectivity
            uint nx=x+dx, ny=y+dy, nz=z+dz;
            if(nx<marginX || nx>=X-marginX || ny<marginY || ny>=Y-marginY || nz<marginZ || nz>=Z-marginZ) continue;
            uint64 index = offsetX[nx]+offsetY[ny]+offsetZ[nz];
            if(sourceData[index] && !targetData[index]) {
                targetData[index] = sourceData[index]; // Marks previously unvisited skeleton voxel
                stack[stackSize++] = short3(nx,ny,nz); // Pushes on stack to remember to visit its neighbours later
                assert_(stackSize<stackBuffer.capacity);
            }
        }
    }
}
class(FloodFill, Operation), virtual VolumeOperation {
    string parameters() const override { return "seed"_; }
    uint outputSampleSize(uint) override { return sizeof(uint8); }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs) override { floodFill(outputs[0], inputs[0], args.value("seed"_,"111111"_)); }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        uint marginSq = TextData(otherInputs[0]->data).integer();
        uint margin = ceil(sqrt(real(marginSq))); //FIXME: use minimalRadius = SquareRoot minimalSqRadius
        floodFill(outputs[0], inputs[0], args.value("seed"_,"111111"_), margin);
    }
};

void intersect(Volume8& target, const Volume8& A, const Volume8& B) {
    assert(A.size() == B.size() && A.tiled() == B.tiled() && A.maximum==1 && B.maximum==1);
    chunk_parallel(target.size(), [&](uint, uint offset, uint size) {
        const uint8* const aData = A + offset;
        const uint8* const bData = B + offset;
        uint8* const targetData = target + offset;
        for(uint i : range(size)) targetData[i] = aData[i] && bData[i];
    });
}
class(Intersect, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint8); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        assert_(inputs[0].sampleSize, "Expected 8bit mask");
        assert_(inputs[1].sampleSize, "Expected 8bit mask");
        intersect(outputs[0], inputs[0], inputs[1]);
    }
};
