#include "rasterize.h"

void rasterize(Volume16& target, const Volume16& source) {
    const uint16* const sourceData = source;
    uint16* const targetData = target;
    const int X=target.x, Y=target.y, Z=target.z, XY = X*Y;
    for(int z=1; z<Z-1; z++) {
        const uint16* const sourceZ = sourceData+z*XY;
        uint16* const targetZ = targetData+z*XY;
        for(int y=1; y<Y-1; y++) {
            const uint16* const sourceZY = sourceZ+y*X;
            uint16* const targetZY = targetZ+y*X;
            for(int x=1; x<X-1; x++) {
                int sqRadius = sourceZY[x];
                int radius = sqrt( sqRadius );
                uint16* const targetZYX = targetZY+x;
                for(int dz=-radius+1; dz<radius; dz++) {
                    uint16* const targetZYXZ = targetZYX+dz*XY;
                    for(int dy=-radius+1; dy<radius; dy++) {
                        uint16* const targetZYXZY = targetZYXZ+dy*X;
                        for(int dx=-radius+1; dx<radius; dx++) {
                            uint16* const targetZYXZYX = targetZYXZY+dx;
                            if(sqRadius > targetZYXZYX[0]) targetZYXZYX[0] = sqRadius; // Rasterizes maximum
                        }
                    }
                }
            }
        }
    }
    target.squared = true;
}
