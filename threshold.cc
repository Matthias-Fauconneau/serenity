#include "threshold.h"
#include "simd.h"

/// Segments by setting values over a fixed threshold to ∞ (2³²-1) and to x² otherwise (for distance X input)
void threshold(Volume32& target, const Volume16& source, float threshold) {
    v4si scaledThreshold = int4( uint(threshold*(source.den/source.num-1)) );
    uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    uint32 sqr[X]; for(uint x=0; x<X; x++) sqr[x]=x*x; // Lookup table of squares
    uint32* const targetData = target;
    for(uint z=0; z<Z; z++) {
        const uint16* const sourceZ = source + z*XY;
        uint32* const targetZ = targetData + z*XY;
        for(uint y=0; y<Y; y++) {
            const uint16* const sourceY = sourceZ + y*X;
            uint32* const targetZY = targetZ + y*X;
            for(uint x=0; x<X; x+=8) {
                storea(targetZY+x, loada(sqr+x) | (scaledThreshold > unpacklo(loada(sourceY+x), _0h)));
                storea(targetZY+x+4, loada(sqr+x+4) | (scaledThreshold > unpackhi(loada(sourceY+x), _0h)));
            }
        }
    }
    // Sets boundary voxels to ensures threshold volume is closed (non-zero borders) to avoid null rows in distance search (FIXME?)
    for(uint z=0; z<Z; z++) {
        uint32* const targetZ = targetData + z*XY;
        for(uint y=0; y<Y; y++) targetZ[y*X]=0*0, targetZ[y*X+X-1]=(X-1)*(X-1); // Sets left/right faces
        for(uint x=0; x<X; x++) targetZ[x]=x*x, targetZ[(Y-1)*X+x]=x*x; // Sets top/bottom faces
    }
    for(uint y=0; y<Y; y++) {
        uint32* const targetY = targetData + y*X;
        for(uint x=0; x<X; x++) targetY[x]=x*x, targetY[(Z-1)*XY+x]=x*x; // Sets front/back faces
    }
    target.num=1, target.den=(target.x-1)*(target.x-1);
}
