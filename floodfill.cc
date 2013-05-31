#include "volume-operation.h"

void floodFill(Volume16& target, const Volume16& source) {
    assert_(source.offsetX && source.offsetY && source.offsetZ);
    const uint16* const sourceData = source;

    uint16* const targetData = target;
    clear(targetData, target.size());
    interleavedLookup(target);
    const uint* const offsetX = target.offsetX, *offsetY = target.offsetY, *offsetZ = target.offsetZ;

    buffer<short3> stackBuffer(1<<25); // 32MiB
    short3* const stack = stackBuffer.data;
    uint stackSize=0;
#if 0
    uint index=0, maximum=0; for(uint i: range(source.size())) if(sourceData[i]>maximum && source.contains(zOrder(i))) index=i, maximum=sourceData[i];
    assert(maximum, index, zOrder(index), maximum);
    stack[stackSize++] = short3(zOrder(index)); // Pushes initial seed (from maximum)
#else
    buffer<byte> markBuffer(target.size()/8); // 128MiB
    byte* const mark = markBuffer.data;
    uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) stack[stackSize++] = short3(x,y,marginZ); // Pushes initial seeds (from top Z face)
    while(stackSize) {
        assert_(stackSize<=stackBuffer.capacity);
        short3& p = stack[--stackSize];
        uint x=p.x, y=p.y, z=p.z;
        for(int dz=-1; dz<=1; dz+=2) for(int dy=-1; dy<=1; dy+=2) for(int dx=-1; dx<=1; dx+=2) { // Visits first neighbours
            uint nx=x+dx, ny=y+dy, nz=z+dz;
            uint index = offsetX[nx]+offsetY[ny]+offsetZ[nz];
            if(source[index] && !(mark[index/8]&(1<<(index%8)))) {
                mark[index/8] |= (1<<(index%8)); // Marks previously unvisited skeleton voxel
                stack[stackSize++] = short3(nx,ny,nz); // Pushes on stack to remember to visit its neighbours later
            }
        }
    }
    for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) {
        uint index = offsetX[x]+offsetY[y]+offsetZ[Z-1-marginZ];
        if(mark[index/8]&(1<<(index%8))) stack[stackSize++] = short3(x,y,Z-1-marginZ); // Only seeds bottom face voxels connected to top face
    }
#endif
    while(stackSize) {
        assert_(stackSize<=stackBuffer.capacity);
        short3& p = stack[--stackSize];
        uint x=p.x, y=p.y, z=p.z;
        for(int dz=-1; dz<=1; dz+=2) for(int dy=-1; dy<=1; dy+=2) for(int dx=-1; dx<=1; dx+=2) { // Visits first neighbours
            uint nx=x+dx, ny=y+dy, nz=z+dz;
            uint index = offsetX[nx]+offsetY[ny]+offsetZ[nz];
            if(sourceData[index] && !targetData[index]) {
                targetData[index] = sourceData[index]; // Marks previously unvisited skeleton voxel
                stack[stackSize++] = short3(nx,ny,nz); // Pushes on stack to remember to visit its neighbours later
            }
        }
    }
}
PASS(FloodFill, uint16, floodFill);
