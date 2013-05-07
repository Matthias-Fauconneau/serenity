#include "rasterize.h"
#include "process.h"

void rasterize(Volume16& target, const Volume16& source) {
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    const int X=target.x, Y=target.y, Z=target.z, XY = X*Y;
    int marginX=target.marginX, marginY=target.marginY, marginZ=target.marginZ;
    clear(targetData, X*Y*Z);
    interleavedLookup(target);
    const uint* const offsetX = target.offsetX;
    const uint* const offsetY = target.offsetY;
    const uint* const offsetZ = target.offsetZ;
    parallel(marginZ,Z-marginZ, [&](uint, uint z) {
        log(z);
        const uint16* const sourceZ = sourceData+z*XY;
        for(int y=marginY; y<Y-marginY; y++) {
            const uint16* const sourceZY = sourceZ+y*X;
            for(int x=marginX; x<X-marginX; x++) {
                int sqRadius = sourceZY[x];
                if(sqRadius==0) continue; // background (rock)
                int radius = sqrt( sqRadius );
                for(int dz=-radius; dz<=radius; dz++) {
                    uint16* const targetZ= targetData + offsetZ[z+dz];
                    for(int dy=-radius; dy<=radius; dy++) {
                        uint16* const targetZY= targetZ + offsetY[y+dy];
                        for(int dx=-radius; dx<=radius; dx++) {
                            uint16* const targetZYX= targetZY + offsetX[x+dx];
                            if(dx*dx+dy*dy+dz*dz<=sqRadius) // Rasterizes ball
                                while(sqRadius > (int)(targetZYX[0]) && !__sync_bool_compare_and_swap(targetZYX, targetZYX[0], sqRadius)); // Stores maximum radius (thread-safe)
                        }
                    }
                }
            }
        }
    } );
    target.squared = true;
}
