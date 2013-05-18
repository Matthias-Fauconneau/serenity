#include "rasterize.h"
#include "process.h"
#include "time.h"

void trim(Volume16& target, const Volume32& pore, const Volume16& maximum) {
    const uint32* const poreData = pore;
    const uint16* const maximumData = maximum;
    uint16* const targetData = target;
    const int X=target.x, Y=target.y, Z=target.z, XY = X*Y;
    int marginX=target.marginX, marginY=target.marginY, marginZ=target.marginZ;
    assert_(!pore.offsetX && !pore.offsetY && !pore.offsetZ);
    const uint* const offsetX = maximum.offsetX, *offsetY = maximum.offsetY, *offsetZ = maximum.offsetZ;
    assert_(offsetX && offsetY && offsetZ);
    //uint maximumTrimmed = 0;
    parallel(marginZ,Z-marginZ, [&](uint, uint z) {
        for(int y=marginY; y<Y-marginY; y++) {
            for(int x=marginX; x<X-marginX; x++) {
                uint pore = poreData[z*XY+y*X+x];
                uint maximum = maximumData[offsetZ[z]+offsetY[y]+offsetX[x]];
                //if(pore != 0xFFFFFFFF) maximumTrimmed=max(maximumTrimmed, maximum);
                targetData[z*XY+y*X+x] = pore == 0xFFFFFFFF ? maximum : 0;
            }
        }
    } );
    //log(sqrt(maximumTrimmed));
    target.squared=true, target.maximum=maximum.maximum;
}
