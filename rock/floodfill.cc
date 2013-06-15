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
    }
};

void floodFill(Volume16& target, const Volume16& source) {
    assert_(source.tiled() && target.tiled());
    const uint16* const sourceData = source;

    uint16* const targetData = target;
    clear(targetData, target.size());
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;

    buffer<short3> stackBuffer(1<<27); // 1024³~128MiB
    short3* const stack = stackBuffer.begin();
    int stackSize=0;
    if(0) { // Seeds from maximum
        uint index=0, maximum=0; for(uint i: range(source.size())) if(sourceData[i]>maximum && source.contains(zOrder(i))) index=i, maximum=sourceData[i];
        assert(maximum, index, zOrder(index), maximum);
        stack[stackSize++] = short3(zOrder(index)); // Pushes initial seed (from maximum)
    } else { // Seeds from top/bottom Z faces
        buffer<byte> markBuffer(target.size()/8, target.size()/8, 0); // 1024³~128MiB
        byte* const mark = markBuffer.begin();
        uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
        uint marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
        {uint z=marginZ;
            for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) {
                uint index = offsetX[x]+offsetY[y]+offsetZ[z];
                if(sourceData[index]) stack[stackSize++] = short3(x,y,z); // Pushes initial seeds (from bottom Z face)
            }
        }
        {uint unused markCount = 0;
            while(stackSize) {
                assert_(uint(stackSize)<=stackBuffer.capacity);
                short3& p = stack[--stackSize];
                uint x=p.x, y=p.y, z=p.z;
                for(auto d: (int[][3]){{0,0,-1},{0,-1,0},{-1,0,0},{1,0,0},{0,1,0},{0,0,1}}) { // Visits first 6 neighbours
                    uint nx=x+d[0], ny=y+d[1], nz=z+d[2];
                    uint index = offsetX[nx]+offsetY[ny]+offsetZ[nz];
                    if(sourceData[index] && !(mark[index/8]&(1<<(index%8)))) {
                        mark[index/8] |= (1<<(index%8)); // Marks previously unvisited skeleton voxel
                        markCount++;
                        stack[stackSize++] = short3(nx,ny,nz); // Pushes on stack to remember to visit its neighbours later
                    }
                }
            }
            assert(markCount); // Null volume
        }
        {uint z=Z-1-marginZ;
            for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) {
                uint index = offsetX[x]+offsetY[y]+offsetZ[z];
                if(mark[index/8]&(1<<(index%8))) {
                    assert_(sourceData[index]);
                    stack[stackSize++] = short3(x,y,z); // Only seeds top face voxels connected to bottom face
                }
            }
        }
    }
    uint unused markCount = 0;
    uint maximum = 0;
    while(stackSize) {
        assert_(uint(stackSize)<=stackBuffer.capacity);
        const short3& p = stack[--stackSize];
        uint x=p.x, y=p.y, z=p.z;
        for(auto d: (int[][3]){{0,0,-1},{0,-1,0},{-1,0,0},{1,0,0},{0,1,0},{0,0,1}}) { // Visits first 6 neighbours
            uint nx=x+d[0], ny=y+d[1], nz=z+d[2];
            uint index = offsetX[nx]+offsetY[ny]+offsetZ[nz];
            if(sourceData[index] && !targetData[index]) {
                targetData[index] = sourceData[index]; // Marks previously unvisited skeleton voxel
                maximum = max<uint>(maximum, targetData[index]);
                markCount++;
                stack[stackSize++] = short3(nx,ny,nz); // Pushes on stack to remember to visit its neighbours later
            }
        }
    }
    assert(markCount); // Null volume
    assert(maximum);
    target.maximum = maximum; // Source maximum might be unconnected (with Z face method)
}
class(FloodFill, Operation), virtual VolumePass<uint16> {
    void execute(const Dict&, VolumeT<uint16>& target, const Volume& source) override { floodFill(target, source); }
};
