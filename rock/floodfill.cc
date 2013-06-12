#include "volume-operation.h"

/// Explicitly clips volume to cylinder by zeroing exterior samples
void cylinderClip(Volume& target, const Volume& source) {
    int X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    int marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    uint radiusSq = (X/2-marginX)*(Y/2-marginY);
    const uint* const offsetX = source.offsetX, *offsetY = source.offsetY, *offsetZ = source.offsetZ;
    bool interleaved = offsetX || offsetY || offsetZ;
    if(interleaved) { assert(offsetX && offsetY && offsetZ); interleavedLookup(target); }
    uint16* const targetData = (uint16*)target.data.data;
    for(int z=0; z<Z; z++) for(int y=0; y<Y; y++) for(int x=0; x<X; x++) {
        uint index = interleaved ? offsetX[x] + offsetY[y] + offsetZ[z] : z*XY + y*X + x;
        uint value = 0;
        if(uint(sq(x-X/2)+sq(y-Y/2)) <= radiusSq && z >= marginZ && z<Z-marginZ) {
            if(source.sampleSize==1) value = ((byte*)source.data.data)[index];
            else if(source.sampleSize==2) value = ((uint16*)source.data.data)[index];
            else if(source.sampleSize==4) value = ((uint32*)source.data.data)[index];
            else error(source.sampleSize);
            assert(value <= source.maximum, value, source.maximum);
        }
        targetData[index] = value;
    }
}
class(CylinderClip, Operation), virtual VolumePass<uint16> { void execute(const Dict&, VolumeT<uint16>& target, const Volume& source) override { cylinderClip(target, source); } };

void floodFill(Volume16& target, const Volume16& source) {
    assert_(source.offsetX && source.offsetY && source.offsetZ);
    const uint16* const sourceData = source;

    uint16* const targetData = target;
    clear(targetData, target.size());
    interleavedLookup(target);
    const uint* const offsetX = target.offsetX, *offsetY = target.offsetY, *offsetZ = target.offsetZ;

    buffer<short3> stackBuffer(1<<25); // 32MiB
    short3* const stack = stackBuffer.begin();
    int stackSize=0;
#if 0
    uint index=0, maximum=0; for(uint i: range(source.size())) if(sourceData[i]>maximum && source.contains(zOrder(i))) index=i, maximum=sourceData[i];
    assert(maximum, index, zOrder(index), maximum);
    stack[stackSize++] = short3(zOrder(index)); // Pushes initial seed (from maximum)
#else
    const int minimumSeedCount = -1;
    buffer<byte> markBuffer(target.size()/8, target.size()/8, 0); // 128MiB
    byte* const mark = markBuffer.begin();
    uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
    uint marginX=source.margin.x, marginY=source.margin.y, marginZ=source.margin.z;
    for(uint z=marginZ; z<Z/2; z++) { // until at least 3 seeds are pushed
        assert_(z<Z-marginZ); // Null volume
        stackSize=0;
        for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) {
            uint index = offsetX[x]+offsetY[y]+offsetZ[z];
            if(sourceData[index]) stack[stackSize++] = short3(x,y,z); // Pushes initial seeds (from bottom Z face)
        }
        if(stackSize>minimumSeedCount) { /*if(stackSize<2*minimumSeedCount)*/ log(stackSize,"seeds for bottom face z="_,z,'/',Z,'-',marginZ); break; }
    }
    {uint unused markCount = 0;
        while(stackSize) {
            assert_(uint(stackSize)<=stackBuffer.capacity);
            short3& p = stack[--stackSize];
            uint x=p.x, y=p.y, z=p.z;
            for(int dz=-1; dz<=1; dz+=2) for(int dy=-1; dy<=1; dy+=2) for(int dx=-1; dx<=1; dx+=2) { // Visits first neighbours
                uint nx=x+dx, ny=y+dy, nz=z+dz;
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
    for(uint z=Z-1-marginZ; z>=Z/2; z--) { // until at least one seed is pushed
        assert_(z>=marginZ, marginZ, Z); // Null volume
        stackSize=0;
        for(uint y=marginY;y<Y-marginY;y++) for(uint x=marginX;x<X-marginX;x++) {
            uint index = offsetX[x]+offsetY[y]+offsetZ[z];
            if(mark[index/8]&(1<<(index%8))) {
                assert_(sourceData[index]);
                stack[stackSize++] = short3(x,y,z); // Only seeds top face voxels connected to bottom face
            }
        }
        if(stackSize>minimumSeedCount) { /*if(stackSize<2*minimumSeedCount)*/ log(stackSize,"seeds for top face z="_,z,'/',Z,'-',marginZ); break; }
    }
#endif
    uint unused markCount = 0;
    uint maximum = 0;
    while(stackSize) {
        assert_(uint(stackSize)<=stackBuffer.capacity);
        short3& p = stack[--stackSize];
        uint x=p.x, y=p.y, z=p.z;
        for(int dz=-1; dz<=1; dz+=2) for(int dy=-1; dy<=1; dy+=2) for(int dx=-1; dx<=1; dx+=2) { // Visits first neighbours
            uint nx=x+dx, ny=y+dy, nz=z+dz;
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
    target.maximum = maximum; // Source maximum might be unconnected
}
class(FloodFill, Operation), virtual VolumePass<uint16> {
    void execute(const Dict& args, VolumeT<uint16>& target, const Volume& source) override { log("minimalSqDiameter",args.at("minimalSqDiameter"_)); floodFill(target, source); }
};
