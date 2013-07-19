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
    chunk_parallel(source.size(), [&](uint offset, uint size) {
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

void connectivityFloodFill(Volume16& target, const Volume16& source, uint connectivitySeedMargin) {
    assert_(source.tiled() && target.tiled());
    const uint16* const sourceData = source;

    assert_(source.margin>int3(1)); // Actually also needs the margin to be 0
    uint16* const targetData = target;
    clear(targetData, target.size());
    const uint64* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;

    buffer<short3> stackBuffer(1<<27); // 1024³~128MiB
    short3* const stack = stackBuffer.begin();
    int stackSize=0;
    // Seeds from top/bottom Z faces
    buffer<byte> markBuffer(target.size()/8, target.size()/8, 0); // 1024³~128MiB
    byte* const mark = markBuffer.begin();
    const uint64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const uint marginX=target.margin.x, marginY=target.margin.y, marginZ=target.margin.z;
    Time time;
    {uint z=marginZ+connectivitySeedMargin;
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
    if((uint64)time>1000) log("1 / 2", time.reset());
    {uint z=Z-1-marginZ-connectivitySeedMargin;
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
    if((uint64)time>1000) log("2 / 2", time.reset());
}
class(ConnectivityFloodFill, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        uint connectivitySeedMarginSq = TextData(otherInputs[0]->data).integer();
        uint connectivitySeedMargin = ceil(sqrt(real(connectivitySeedMarginSq))); //FIXME: use minimalRadius = SquareRoot minimalSqRadius
        connectivityFloodFill(outputs[0], inputs[0], connectivitySeedMargin);
    }
};

void floodFill(Volume8& target, const Volume8& source) {
    assert_(source.tiled() && target.tiled());
    const uint8* const sourceData = source;

    uint8* const targetData = target;
    clear(targetData, target.size());
    const uint64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    const uint marginX=target.margin.x, marginY=target.margin.y, marginZ=target.margin.z;
    const uint64* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;

    buffer<short3> stackBuffer(1<<27); // 1024³~128MiB
    short3* const stack = stackBuffer.begin();
    int stackSize=0;
    for(uint z=marginZ;z<Z-marginZ;z++) for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) { // Seeds all faces
        if(x==marginX || x==X-marginX-1 || y==marginY || y==Y-marginY-1 || z==marginZ || z==Z-marginZ-1) stack[stackSize++] = short3(x,y,z);
    }
    while(stackSize) {
        const short3& p = stack[--stackSize];
        uint x=p.x, y=p.y, z=p.z;
        for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) { // 26-way connectivity
            uint nx=x+dx, ny=y+dy, nz=z+dz;
            if(nx>=X || ny>=Y || nz>=Z) continue;
            uint index = offsetX[nx]+offsetY[ny]+offsetZ[nz];
            if(sourceData[index] && !targetData[index]) {
                targetData[index] = sourceData[index]; // Marks previously unvisited skeleton voxel
                stack[stackSize++] = short3(nx,ny,nz); // Pushes on stack to remember to visit its neighbours later
            }
        }
    }
}
class(FloodFill, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint8); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        floodFill(outputs[0], inputs[0]);
    }
};
