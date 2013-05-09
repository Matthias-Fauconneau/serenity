#include "threshold.h"
#include "simd.h"

void threshold(Volume32& pore, Volume32& rock, const Volume16& source, float threshold) {
    v4si scaledThreshold = set1(threshold*source.maximum);
    uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    uint32 sqr[X]; for(uint x=0; x<X; x++) sqr[x]=x*x; // Lookup table of squares
    uint32* const poreData = pore;
    uint32* const rockData = rock;
    for(uint z=0; z<Z; z++) {
        const uint16* const sourceZ = source + z*XY;
        uint32* const poreZ = poreData + z*XY;
        uint32* const rockZ = rockData + z*XY;
        for(uint y=0; y<Y; y++) {
            const uint16* const sourceY = sourceZ + y*X;
            uint32* const poreZY = poreZ + y*X;
            uint32* const rockZY = rockZ + y*X;
            for(uint x=0; x<X; x+=8) {
                storea(poreZY+x, loada(sqr+x) | (scaledThreshold > unpacklo(loada(sourceY+x), _0h)));
                storea(poreZY+x+4, loada(sqr+x+4) | (scaledThreshold > unpackhi(loada(sourceY+x), _0h)));
                storea(rockZY+x, loada(sqr+x) | (unpacklo(loada(sourceY+x), _0h) > scaledThreshold));
                storea(rockZY+x+4, loada(sqr+x+4) | (unpackhi(loada(sourceY+x), _0h) > scaledThreshold));
            }
        }
    }
    // Sets boundary voxels to ensures threshold volume is closed (non-zero borders) to avoid null rows in distance search (FIXME?)
    for(uint z=0; z<Z; z++) {
        uint32* const poreZ = poreData + z*XY;
        uint32* const rockZ = rockData + z*XY;
        for(uint y=0; y<Y; y++) rockZ[y*X]=poreZ[y*X]=0*0, rockZ[y*X+X-1]=poreZ[y*X+X-1]=(X-1)*(X-1); // Sets left/right faces
        for(uint x=0; x<X; x++) rockZ[x]=poreZ[x]=x*x, rockZ[(Y-1)*X+x]=poreZ[(Y-1)*X+x]=x*x; // Sets top/bottom faces
    }
    for(uint y=0; y<Y; y++) {
        uint32* const poreY = poreData + y*X;
        uint32* const rockY = rockData + y*X;
        for(uint x=0; x<X; x++) rockY[x]=poreY[x]=x*x, rockY[(Z-1)*XY+x]=poreY[(Z-1)*XY+x]=x*x; // Sets front/back faces
    }
    pore.maximum=(pore.x-1)*(pore.x-1);
    rock.maximum=(rock.x-1)*(rock.x-1);
}
