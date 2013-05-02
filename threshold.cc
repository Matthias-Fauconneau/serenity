#include "threshold.h"

/// Computes density threshold as local (around 0.45=115) minimum of density histogram.
float computeDensityThreshold(const Volume16& volume) {
    const uint binCount = 0x100; // density values are quantized on 8bit
    uint count[binCount]={};  // Density histogram (i.e voxel count for each density value)
    uint X=volume.x, Y=volume.y, Z=volume.z, XY=X*Y;
    uint marginX=volume.marginX, marginY=volume.marginY, marginZ=volume.marginZ;
    uint radiusSq=(X/2-marginX)*(X/2-marginX);
    //#pragma omp parallel for
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint16* sourceZ = volume+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* sourceY = sourceZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                if((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2) >= radiusSq) continue;
                const uint16* sourceX = sourceY+x;
                uint density = sourceX[0]*(0x100*volume.num)/volume.den; // Scales to 8bit
                if(density > 0x100) { /*qDebug()<<"clip"<<density<<s[0]<<volume.num<<volume.den; FIXME!*/ break; }
                count[density]++;
            }
        }
    }
    float densityThreshold=0;
    uint minimum = -1;
    for(uint density=115-9; density<=115+9; density++) { //FIXME: find local minimum without these bounds
        if(count[density] < minimum) {
            densityThreshold = (float) density / binCount;
            minimum = count[density];
        }
    }
    return densityThreshold;
}

/// Segments by setting values over a fixed threshold to ∞ (2³²-1) and to x² otherwise
void threshold(Volume32& target, const Volume16& source, float threshold) {
    uint16 scaledThreshold = threshold*(source.den/source.num-1);
    uint X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    uint32* const targetData = target;
    //#pragma omp parallel for
    for(uint z=0; z<Z; z++) {
        const uint16* const sourceZ = source + z*XY;
        uint32* const targetZ = targetData + z*XY;
        for(uint y=0; y<Y; y++) {
            const uint16* const sourceY = sourceZ + y*X;
            uint32* const targetY = targetZ + y*X;
            for(uint x=0; x<X; x++) {
                uint16 value = sourceY[x];
                targetY[x] = value < scaledThreshold ? 0xFFFFFFFF : x*x;
            }
        }
    }
    // Sets boundary voxels to ensures threshold volume is closed (non-zero borders) to avoid null rows in distance search (FIXME?)
    for(uint y=0; y<target.y; y++) for(uint z=0; z<target.z; z++) targetData[z*XY+y*X]=0*0, targetData[z*XY+y*X+target.x-1]=(target.x-1)*(target.x-1); // Sets left/right faces
    for(uint x=0; x<target.x; x++) for(uint z=0; z<target.z; z++) targetData[z*XY+x]=x*x, targetData[z*XY+(target.y-1)*X+x]=x*x; // Sets top/bottom faces
    for(uint x=0; x<target.x; x++) for(uint y=0; y<target.y; y++) targetData[y*X+x]=x*x, targetData[(target.z-1)*XY+y*X+x]=x*x; // Sets front/back faces
    target.num=1, target.den=target.x*target.x;
}
