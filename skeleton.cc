#include "skeleton.h"
#include "process.h"

inline void compare(uint16* const skel, const uint16* const xf, const uint16* const yf, const uint16* const zf, int x, int y, int z, int dx, int dy, int dz, int da) {
    int xf0=xf[0], yf0=yf[0], zf0=zf[0], xfd=xf[da], yfd=yf[da], zfd=zf[da], xd=x+dx, yd=y+dy, zd=z+dz;
    int x0d=xf0-xfd, y0d=yf0-yfd, z0d=zf0-zfd;
    int sqNorm = sqr(x0d) + sqr(y0d) + sqr(z0d);
    int inprod = - dx*x0d - dy*y0d - dz*z0d;
    int dx0d = xf0-x+xfd-xd, dy0d = yf0-y+yfd-yd, dz0d = zf0-z+zfd-zd;
    float norm = sqrt( sqr(dx0d) + sqr(dy0d) + sqr(dz0d) );
    const int lambda = 3; /*Constant pruning parameter: √(1/slope) < λ < minimum throat radius*/
    if(sqNorm > lambda*lambda && sqNorm > 2*inprod + norm + 1.5f) {
        int crit = x0d*dx0d + y0d*dx0d + z0d*dx0d;
        if(crit>=0) { int r = sqr(xf0-x) + sqr(yf0-y) + sqr(zf0-z); assert(r<0x10000); skel[0] = r; }
        if(crit<=0) { int r = sqr(xfd-xd) + sqr(yfd-yd) + sqr(zfd-zd); assert(r<0x10000); skel[da] = r; }
    }
}

void integerMedialAxis(Volume16& target, const Volume16& positionX, const Volume16& positionY, const Volume16& positionZ) {
    const uint16* const xPositionData = positionX;
    const uint16* const yPositionData = positionY;
    const uint16* const zPositionData = positionZ;
    uint16* const targetData = target;
    const uint X=target.x, Y=target.y, Z=target.z, XY = X*Y;
    uint marginX=max(1u,target.marginX), marginY=max(1u,target.marginY), marginZ=max(1u,target.marginZ);
    clear<uint16>(target, target.size(), 0); //FIXME: only clear margins
    for(uint z=marginZ; z<Z-marginZ; z++) {
    //parallel(marginZ, Z-marginZ, [&](uint, uint z) { //FIXME: will conflict
        const uint16* const xPositionZ = xPositionData+z*XY;
        const uint16* const yPositionZ = yPositionData+z*XY;
        const uint16* const zPositionZ = zPositionData+z*XY;
        uint16* const targetZ = targetData+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* const xPositionZY = xPositionZ+y*X;
            const uint16* const yPositionZY = yPositionZ+y*X;
            const uint16* const zPositionZY = zPositionZ+y*X;
            uint16* const targetZY = targetZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                const uint16* const xf = xPositionZY+x;
                const uint16* const yf = yPositionZY+x;
                const uint16* const zf = zPositionZY+x;
                uint16* const skel = targetZY+x;

                if(xf[0]<0xFFFF) {
                    skel[0] = 1;
                    if(xf[-1]<0xFFFF) compare(skel,xf,yf,zf,x,y,z, -1,0,0, -1);
                    if(xf[-(int)X]<0xFFFF) compare(skel,xf,yf,zf,x,y,z, 0,-1,0, -X);
                    if(xf[-(int)XY]<0xFFFF) compare(skel,xf,yf,zf,x,y,z, 0,0,-1, -XY);
                } else {
                    assert(yf[0]==0xFFFF);
                    assert(zf[0]==0xFFFF);
                    //skel[0] = 0; //FIXME: only clear margins
                }
            }
        }
    } //);
    target.marginX = marginX, target.marginY = marginY, target.marginZ = marginZ;
    target.maximum = maximum(target), target.squared=true;
}
