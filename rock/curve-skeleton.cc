#include "volume-operation.h"
#include "thread.h"

/// Removes voxels matching one of 8 thinning templates until the curve skeleton is found
void templateThin(Volume8& target, const Volume8& source) {
    copy(mref<uint>(target, target.size()), source);

    uint8* const targetData = target;
    const int64 X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY = X*Y;
    const uint marginX=target.margin.x+1, marginY=target.margin.y+1, marginZ=target.margin.z+1;
    //parallel(marginZ, Z-marginZ, [&](uint, uint z) {
    for(uint z: range(marginZ, Z-marginZ)) {
        uint8* const targetZ = targetData+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            uint8* const targetZY = targetZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                uint8* const voxel = targetZY+x;

            }
        }
    }
    //});
    target.margin.x = marginX, target.margin.y = marginY, target.margin.z = marginZ;
}
defineVolumePass(CurveSkeleton, uint16, templateThin);

