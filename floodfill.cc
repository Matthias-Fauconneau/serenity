#include "volume-operation.h"

void floodFill(Volume16& target, const Volume16& source) {
    assert_(source.offsetX && source.offsetY && source.offsetZ);
    const uint16* const sourceData = source;
    uint index=0, maximum=0; for(uint i: range(source.size())) if(sourceData[i]>maximum && source.contains(zOrder(i))) index=i, maximum=sourceData[i];
    assert(maximum, index, zOrder(index), maximum);

    uint16* const targetData = target;
    clear(targetData, target.size());
    interleavedLookup(target);
    const uint* const offsetX = target.offsetX, *offsetY = target.offsetY, *offsetZ = target.offsetZ;
    buffer<short3> stackBuffer(1<<25); //32MB
    short3* const stack = stackBuffer;
    uint stackSize=0;
    stack[stackSize++] = short3(zOrder(index)); // Pushes initial seed
    while(stackSize) {
        assert_(stackSize<=stackBuffer.capacity);
        short3& p = stack[--stackSize];
        uint x=p.x, y=p.y, z=p.z;
        for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) {
            uint nx=x+dx, ny=y+dy, nz=z+dz;
            uint index = offsetX[nx]+offsetY[ny]+offsetZ[nz];
            if(source[index] && !target[index]) {
                target[index] = source[index]; // Marks previously unvisited skeleton voxel
                stack[stackSize++] = short3(nx,ny,nz); // Pushes on stack to remember to visit its neighbours later
            }
        }
    }
}

class(FloodFill, Operation), virtual VolumePass<uint16> { void execute(const map<ref<byte>, Variant>&, VolumeT<uint16>& target, const Volume& source) override { floodFill(target, source); } };
