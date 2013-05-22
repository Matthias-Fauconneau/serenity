#include "maximum.h"
#include "thread.h"
#include "simd.h"

/// Returns the field of the radii of the maximum sphere enclosing each voxel and fitting within the boundaries
void maximum(Volume16& target, const Volume16& source) {
    uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    uint marginX=max(1u,source.marginX), marginY=max(1u,source.marginY), marginZ=max(1u,source.marginZ);
    const uint16* const data = source;
    const uint* const offsetX = source.offsetX;
    const uint* const offsetY = source.offsetY;
    const uint* const offsetZ = source.offsetZ;
    assert_(offsetX && offsetY && offsetZ);
    uint16* const targetData = target;
    clear(targetData, Z*Y*X); //TODO: tiled target, Z-order
    int maxStack=0; uint maxList=0;
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
    //for(uint z : range(marginZ, Z-marginZ)) {
        uint16* const targetZ = targetData+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            uint16* const targetZY = targetZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                uint maximumD=data[offsetX[x]+offsetY[y]+offsetZ[z]];
                uint16& target = targetZY[x];
                if(!maximumD || target) continue;
                struct { uint x,y,z,d; } stack[256]; // Used to follow multiple equal paths
                struct { uint x,y,z,d; } list[512]; uint listSize=0; // Remember visited points to be written after successful walk
                stack[0] = {x,y,z,maximumD};
                for(int i=0; i>=0; i--) { // Depth-first search distance field for local maximum
                    uint x=stack[i].x, y=stack[i].y, z=stack[i].z, d=stack[i].d;
                    if(d >= maximumD) { // Might not be maximum anymore
                        for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) {
                            uint nx=x+dx, ny=y+dy, nz=z+dz;
                            uint d = data[offsetX[nx]+offsetY[ny]+offsetZ[nz]];
                            if(d >= maximumD) {
                                maximumD=d;
                                uint16& target = targetData[nz*XY+ny*X+nx];
                                if(target==0xFFFF) continue; // Already visited on this walk
                                if(target!=0) { if(target>maximumD) maximumD=target; continue; } // Already visited on a previous walk
                                target=0xFFFF;
                                assert_(i<256);
                                stack[i++]={nx,ny,nz,d};
                                maxStack=max(maxStack, i);
                                assert(listSize<512);
                                list[listSize++]={nx,ny,nz,d};
                                maxList = max(maxList, listSize);
                            }
                        }
                    }
                }
                for(uint i=0; i<listSize; i++) targetData[list[i].z*XY+list[i].y*X+list[i].x] = maximumD;
                target = maximumD;
            }
        }
    });
    assert_(maxStack<256, maxList<512);
    target.marginX=marginX, target.marginY=marginY, target.marginZ=marginZ;
}
