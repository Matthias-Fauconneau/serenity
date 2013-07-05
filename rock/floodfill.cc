#include "volume-operation.h"
#include "thread.h"

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
    chunk_parallel(source.size(), [&](uint offset, uint size) {
        const uint16* const sourceData = source + offset;
        uint16* const targetData = target + offset;
        for(uint i : range(size)) targetData[i] = sourceData[i] > threshold ? sourceData[i] : 0;
    });
}
class(ThresholdClip, Operation), virtual VolumeOperation {
    string parameters() const override { return "clipThreshold"_; }
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        uint clipThreshold;
        if(args.contains("clipThreshold"_) && isInteger(args.at("clipThreshold"_))) clipThreshold = toInteger(args.at("clipThreshold"_));
        else clipThreshold = TextData(otherInputs[0]->data).integer();
        thresholdClip(outputs[0], inputs[0], clipThreshold);
        outputs[0].margin += ceil(sqrt((real)clipThreshold)); //HACK: pruned skeleton cannot reach boundaries (needed for floodfill connectivity)
    }
};

void floodFill(Volume16& target, const Volume16& source) {
    assert_(source.tiled() && target.tiled());
    const uint16* const sourceData = source;

    assert_(source.margin>int3(1)); // Actually also needs the margin to be 0
    uint16* const targetData = target;
    clear(targetData, target.size());
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;

    buffer<short3> stackBuffer(1<<27); // 1024³~128MiB
    short3* const stack = stackBuffer.begin();
    int stackSize=0;
    // Seeds from top/bottom Z faces
    buffer<byte> markBuffer(target.size()/8, target.size()/8, 0); // 1024³~128MiB
    byte* const mark = markBuffer.begin();
    uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    uint marginX=target.margin.x, marginY=target.margin.y, marginZ=target.margin.z;
    {uint z=marginZ;
        for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) {
            uint index = offsetX[x]+offsetY[y]+offsetZ[z];
            if(sourceData[index]) stack[stackSize++] = short3(x,y,z); // Pushes initial seeds (from bottom Z face)
        }
    }
    while(stackSize) {
        short3& p = stack[--stackSize];
        uint x=p.x, y=p.y, z=p.z;
        for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) { // 26-way connectivity
            uint nx=x+dx, ny=y+dy, nz=z+dz;
            uint index = offsetX[nx]+offsetY[ny]+offsetZ[nz];
            if(sourceData[index] && !(mark[index/8]&(1<<(index%8)))) {
                mark[index/8] |= (1<<(index%8)); // Marks previously unvisited skeleton voxel
                stack[stackSize++] = short3(nx,ny,nz); // Pushes on stack to remember to visit its neighbours later
            }
        }
    }
    {uint z=Z-1-marginZ;
        for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) {
            uint index = offsetX[x]+offsetY[y]+offsetZ[z];
            if(mark[index/8]&(1<<(index%8))) {
                stack[stackSize++] = short3(x,y,z); // Only seeds top face voxels connected to bottom face
            }
        }
    }
    while(stackSize) {
        const short3& p = stack[--stackSize];
        uint x=p.x, y=p.y, z=p.z;
        for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) { // 26-way connectivity
            uint nx=x+dx, ny=y+dy, nz=z+dz;
            uint index = offsetX[nx]+offsetY[ny]+offsetZ[nz];
            if(sourceData[index] && !targetData[index]) {
                targetData[index] = sourceData[index]; // Marks previously unvisited skeleton voxel
                stack[stackSize++] = short3(nx,ny,nz); // Pushes on stack to remember to visit its neighbours later
            }
        }
    }
}
class(FloodFill, Operation), virtual VolumePass<uint16> {
    void execute(const Dict&, VolumeT<uint16>& target, const Volume& source) override {
        floodFill(target, source);
    }
};
