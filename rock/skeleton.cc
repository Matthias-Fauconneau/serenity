#include "volume-operation.h"
#include "thread.h"

inline void compare(uint16* const skel, const uint16* const xf, const uint16* const yf, const uint16* const zf, int x, int y, int z, int dx, int dy, int dz, int da, int minimalSqDiameter) {
    int xf0=xf[0], yf0=yf[0], zf0=zf[0]; // First feature point
    int xfd=xf[da], yfd=yf[da], zfd=zf[da]; // Second feature point
    int x0d=xf0-xfd, y0d=yf0-yfd, z0d=zf0-zfd; // Vector between feature points
    int sqNorm = sq(x0d) + sq(y0d) + sq(z0d); // Squared distance between feature points
    int xd=x+dx, yd=y+dy, zd=z+dz; // Second origin point
    int dx0d = xf0-x+xfd-xd, dy0d = yf0-y+yfd-yd, dz0d = zf0-z+zfd-zd; // Bisector (vector bisecting
    int sqDistance = sq(dx0d) + sq(dy0d) + sq(dz0d);
    int inprod = - dx*x0d - dy*y0d - dz*z0d;
    float norm = sqrt(float(sqDistance));
    // Prune using all methods (as rasterization is the bottleneck)
    if( sqNorm > minimalSqDiameter &&  // Constant pruning: feature point far enough apart (may filter small features)
         sqNorm > sqDistance && // Linear (angle) pruning: tan(α/2) = o/2a > 1 <=> α > 2atan(2) > 53° (may cut corners, effective when sqDistance > sqNorm > sqDiameter)
         sqNorm >  2*inprod + norm + 1.5f // Square root pruning: No parameters (may disconnect skeleton)
            ) {
        int crit = x0d*dx0d + y0d*dx0d + z0d*dx0d;
        if(crit>=0) { int r = sq(xf0-x) + sq(yf0-y) + sq(zf0-z); skel[0] = r; }
        if(crit<=0) { int r = sq(xfd-xd) + sq(yfd-yd) + sq(zfd-zd); skel[da] = r; }
    }
}

/// Computes integer medial axis
void integerMedialAxis(Volume16& target, const Volume16& positionX, const Volume16& positionY, const Volume16& positionZ, int minimalSqDiameter) {
    assert_(minimalSqDiameter>=3);
    const uint16* const xPositionData = positionX;
    const uint16* const yPositionData = positionY;
    const uint16* const zPositionData = positionZ;
    uint16* const targetData = target;
    clear(targetData, target.size());
    const uint X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY = X*Y;
    uint marginX=target.margin.x+1, marginY=target.margin.y+1, marginZ=target.margin.z+1;
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
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
                    if(xf[-1]<0xFFFF) compare(skel,xf,yf,zf,x,y,z, -1,0,0, -1, minimalSqDiameter);
                    if(xf[-(int)X]<0xFFFF) compare(skel,xf,yf,zf,x,y,z, 0,-1,0, -X, minimalSqDiameter);
                    if(xf[-(int)XY]<0xFFFF) compare(skel,xf,yf,zf,x,y,z, 0,0,-1, -XY, minimalSqDiameter);
                }
            }
        }
    });
    target.margin.x = marginX, target.margin.y = marginY, target.margin.z = marginZ;
    target.maximum = maximum(target), target.squared=true;
}

/// Keeps only voxels on the medial axis of the pore space (integer medial axis skeleton ~ centers of maximal spheres)
class(Skeleton, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override { integerMedialAxis(outputs[0],inputs[0],inputs[1],inputs[2], 3); }
};
