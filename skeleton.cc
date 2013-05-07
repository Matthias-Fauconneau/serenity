#include "skeleton.h"

void integerMedialAxis(Volume16& target, const Volume16& positionX, const Volume16& positionY, const Volume16& positionZ) {
    const uint16* const xPositionData = positionX;
    const uint16* const yPositionData = positionY;
    const uint16* const zPositionData = positionZ;
    uint16* const targetData = target;
    const int X=target.x, Y=target.y, Z=target.z, XY = X*Y;
    for(int z=1; z<Z-1; z++) {
        const uint16* const xPositionZ = xPositionData+z*XY;
        const uint16* const yPositionZ = yPositionData+z*XY;
        const uint16* const zPositionZ = zPositionData+z*XY;
        uint16* const targetZ = targetData+z*XY;
        for(int y=1; y<Y-1; y++) {
            const uint16* const xPositionZY = xPositionZ+y*X;
            const uint16* const yPositionZY = yPositionZ+y*X;
            const uint16* const zPositionZY = zPositionZ+y*X;
            uint16* const targetZY = targetZ+y*X;
            for(int x=1; x<X-1; x++) {
                const uint16* const xf = xPositionZY+x;
                const uint16* const yf = yPositionZY+x;
                const uint16* const zf = zPositionZY+x;
                uint16* const skel = targetZY+x;
#define compare(dx, dy, dz, da) ({\
    int xf0=xf[0], yf0=yf[0], zf0=zf[0], xfd=xf[da], yfd=yf[da], zfd=zf[da], xd=x+dx, yd=y+dy, zd=z+dz; \
    int sqNorm = sqr(xf0-xfd) + sqr(yf0-yfd) + sqr(zf0-zfd); \
    int inprod = - dx*(xf0-xfd) - dy*(yf0-yfd) - dz*(zf0-zfd); \
    float norm = sqrt( sqr(xf0-x+xfd-xd) + sqr(yf0-y+yfd-yd) + sqr(zf0-z+zfd-zd) ); \
    if(sqNorm > 1 && sqNorm > 2*inprod + norm + 1.5f) { \
                int crit = (xf0-xfd)*(xf0-x+xfd-xd) + (yf0-yfd)*(yf0-y+yfd-yd) + (zf0-zfd)*(zf0-z+zfd-zd); \
                if(crit>=0) skel[0] = sqr(xf0-x) + sqr(yf0-y) + sqr(zf0-y); if(crit<=0) skel[da] = sqr(xfd-xd) + sqr(yfd-yd) + sqr(zfd-yd); } })

                if(xf[0]<0xFFFF) {
                    skel[0] = 1;
                    if(xf[-1]<0xFFFF) compare(-1, 0, 0, -1);
                    if(yf[-X]<0xFFFF) compare(0, -1, 0, -X);
                    if(zf[-XY]<0xFFFF) compare(0, 0, -1, -XY);
                } else {
                    assert(yf[0]==0xFFFF);
                    assert(zf[0]==0xFFFF);
                    skel[0] = 0;
                }
            }
        }
    }
    target.num=1, target.den = maximum(target), target.squared=true;
}
