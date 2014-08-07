#include "volume-operation.h"
#include "thread.h"

/// Evaluates number of foreground neighbours
void connectivity(Volume8& target, const Volume8& source) {
    const uint8* const sourceData = source;
    uint8* const targetData = target;
    const int64 X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY = X*Y;
    const uint marginX=source.margin.x+1, marginY=source.margin.y+1, marginZ=source.margin.z+1;
    assert_(!source.tiled());
    int64 offsets[26] = { -XY-X-1, -XY-X, -XY-X+1, -XY-1, -XY, -XY+1, -XY+X-1, -XY+X, -XY+X+1,
                             -X-1,    -X,    -X+1,    -1,         +1,    +X-1,    +X,    +X+1,
                          +XY-X-1, +XY-X, +XY-X+1, +XY-1, +XY, +XY+1, +XY+X-1, +XY+X, +XY+X+1 };
    uint maximums[coreCount] = {};
    parallel(marginZ, Z-marginZ, [&](uint id, uint z) {
        uint maximum = 0;
        uint const indexZ = z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            uint const indexZY = indexZ + y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                uint const index = indexZY + x;
                const uint8* const source = sourceData + index;
                if(source[0]) {
                    uint connectivity = 0; // 0 means both background and unconnected points
                    for(uint i: range(26)) if(source[offsets[i]]) connectivity++;
                    targetData[index] = connectivity;
                    if(connectivity > maximum) maximum = connectivity;
                } else {
                    targetData[index] = 0;
                }
            }
        }
        maximums[id] = ::max(maximums[id], maximum);
    });
    target.maximum = max(ref<uint>(maximums,coreCount));
    target.margin.x = marginX, target.margin.y = marginY, target.margin.z = marginZ;
}
defineVolumePass(Connectivity, uint8, connectivity);
