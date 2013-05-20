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
    uint marginX=align(4,min(1u,source.marginX))+1, marginY=align(4,min(1u,source.marginY))+1, marginZ=align(4,min(1u,source.marginZ))+1;
    for(uint z=marginZ; z<Z-marginZ; z++) {
        uint32* const poreZ = poreData + z*XY;
        uint32* const rockZ = rockData + z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            for(uint x=0; x<=marginX; x++) rockZ[y*X+x]=poreZ[y*X+x]=x*x; // Sets right face
            for(uint x=X-marginX; x<Z; x++) rockZ[y*X+x]=poreZ[y*X+x]=x*x; // Sets left face
        }
        for(uint x=marginX; x<X-marginX; x++) {
            for(uint y=0; y<=marginY; y++) rockZ[y*X+x]=poreZ[y*X+x]=x*x; // Sets top face
            for(uint y=Y-marginY; y<Z; y++) rockZ[y*X+x]=poreZ[y*X+x]=x*x; // Sets bottom face
        }
    }
    for(uint y=marginY; y<Y-marginY; y++) {
        uint32* const poreY = poreData + y*X;
        uint32* const rockY = rockData + y*X;
        for(uint x=marginX; x<X-marginX; x++) {
            for(uint z=0; z<=marginZ; z++) rockY[z*XY+x]=poreY[z*XY+x]=x*x; // Sets front face
            for(uint z=Z-marginZ; z<Z; z++) rockY[z*XY+x]=poreY[z*XY+x]=x*x; // Sets back face
        }
    }
    pore.marginX=marginX-1, pore.marginY=marginY-1, pore.marginZ=marginZ-1;
    rock.marginX=marginX-1, rock.marginY=marginY-1, rock.marginZ=marginZ-1;
#if 1
    pore.maximum=0xFFFFFFFF, rock.maximum=0xFFFFFFFF;  // for the assert
#else
    pore.maximum=(pore.x-1)*(pore.x-1), rock.maximum=(rock.x-1)*(rock.x-1); // for visualization
#endif
}
