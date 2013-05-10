#include "threshold.h"
#include "simd.h"
#include "process.h"

void threshold(Volume32& pore, Volume32& rock, const Volume16& source, float threshold) {
    v4si scaledThreshold = set1(threshold*source.maximum);
    uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    assert(X%8==0);
    uint32 sqr[X]; for(uint x=0; x<X; x++) sqr[x]=x*x; // Lookup table of squares
    uint32* const poreData = pore;
    uint32* const rockData = rock;
    parallel(Z, [&](uint, uint z) {
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
    });
    // Sets boundary voxels to ensures threshold volume is closed (non-zero borders) to avoid null/full rows in distance search
    int marginX=floor(4,source.marginX), marginY=floor(4,source.marginY), marginZ=floor(4,source.marginZ);
    for(uint z=marginZ; z<Z-marginZ; z++) {
        uint32* const poreZ = poreData + z*XY;
        uint32* const rockZ = rockData + z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            rockZ[y*X+marginX]=poreZ[y*X+marginX]=marginX*marginX; // Sets right face
            rockZ[y*X+X-marginX-1]=poreZ[y*X+X-marginX-1]=(X-marginX-1)*(X-marginX-1); // Sets left face
        }
        for(uint x=marginX; x<X-marginX; x++) {
            rockZ[marginY*X+x]=poreZ[marginY*X+x]=x*x; // Sets top face
            rockZ[(Y-marginY-1)*X+x]=poreZ[(Y-marginY-1)*X+x]=x*x; // Sets bottom face
        }
    }
    for(uint y=marginY; y<Y-marginY; y++) {
        uint32* const poreY = poreData + y*X;
        uint32* const rockY = rockData + y*X;
        for(uint x=marginX; x<X-marginX; x++) {
            rockY[marginZ*XY+x]=poreY[marginZ*XY+x]=x*x; // Sets front face
            rockY[(Z-marginZ-1)*XY+x]=poreY[(Z-marginZ-1)*XY+x]=x*x; // Sets back face
        }
    }
    pore.maximum=0xFFFFFFFF, rock.maximum=0xFFFFFFFF;
}
