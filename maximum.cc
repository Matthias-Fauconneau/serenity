#include "maximum.h"
#include "process.h"
#include "simd.h"

template<> string str(const v4sf& v) { return "("_+str(extractf(v,0))+", "_+str(extractf(v,1))+", "_+str(extractf(v,2))+", "_+str(extractf(v,3))+")"_; }

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
    clear(targetData, Z*Y*X); //if memoize //TODO: tiled target, Z-order
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        uint16* const targetZ = targetData+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            uint16* const targetZY = targetZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                int currentD=data[offsetX[x]+offsetY[y]+offsetZ[z]];
                if(currentD) for(int currentX = x, currentY = y, currentZ = z;;) { // Ascent distance field until reaching a local maximum
                    {int nextD = targetData[currentZ*XY+currentY*X+currentX]; if(nextD) { currentD=nextD; break; }} // Reuse already computed paths //FIXME: break result ?!
                    int nextD=currentD, nextX=0, nextY=0, nextZ=0;
                    for(int dz=-1; dz<=1; dz++) for(int dy=-1; dy<=1; dy++) for(int dx=-1; dx<=1; dx++) {
                        int stepX=currentX+dx, stepY=currentY+dy, stepZ=currentZ+dz;
                        int d = data[offsetX[stepX]+offsetY[stepY]+offsetZ[stepZ]];
                        if(d > nextD) nextD=d, nextX=stepX, nextY=stepY, nextZ=stepZ;
                    }
                    if(nextD==currentD) break;
                    currentD=nextD, currentX=nextX, currentY=nextY, currentZ=nextZ;
                }
                targetZY[x] = currentD;
            }
        }
    });
    target.marginX=marginX, target.marginY=marginY, target.marginZ=marginZ;
}
