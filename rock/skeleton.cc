#include "volume-operation.h"
#include "thread.h"

inline void compare(uint16* const skel, const short3* const pos, int x, int y, int z, int dx, int dy, int dz, int da, int minimalSqDiameter) {
    int xf0=pos[0].x, yf0=pos[0].y, zf0=pos[0].z; // First feature point
    int xfd=pos[da].x, yfd=pos[da].y, zfd=pos[da].z; // Second feature point
    int x0d=xf0-xfd, y0d=yf0-yfd, z0d=zf0-zfd; // Vector between feature points
    int sqNorm = sq(x0d) + sq(y0d) + sq(z0d); // Squared distance between feature points
    int xd=x+dx, yd=y+dy, zd=z+dz; // Second origin point
    int dx0d = xf0-x+xfd-xd, dy0d = yf0-y+yfd-yd, dz0d = zf0-z+zfd-zd; // Bisector
    int sqDistance = sq(dx0d) + sq(dy0d) + sq(dz0d);
    int inprod = - dx*x0d - dy*y0d - dz*z0d;
    float norm = sqrt(float(sqDistance));
    // Prune using all methods (as rasterization is the bottleneck)
    if( sqNorm > minimalSqDiameter  // Constant pruning: feature point far enough apart (may filter small features)
         //&& sqNorm > sqDistance // Linear (angle) pruning: tan(α/2) = o/2a > 1 <=> α > 2atan(2) > 126° (may cut corners, effective when sqDistance > sqNorm > sqDiameter)
         && sqNorm >  2*inprod + norm + 1.5f // Square root pruning: No parameters (may disconnect skeleton)
            ) {
        int crit = x0d*dx0d + y0d*dx0d + z0d*dx0d;
        if(crit>=0) { int r = sq(xf0-x) + sq(yf0-y) + sq(zf0-z); skel[0] = r; }
        if(crit<=0) { int r = sq(xfd-xd) + sq(yfd-yd) + sq(zfd-zd); skel[da] = r; }
    }
}

/// Computes integer medial axis
void integerMedialAxis(Volume16& target, const Volume3x16& position, int minimalSqDiameter) {
    const short3* const positionData = position;
    uint16* const targetData = target;
    mref<uint16>(target, target.size()).clear(0);
    const int64 X=target.sampleCount.x, Y=target.sampleCount.y, Z=target.sampleCount.z, XY = X*Y;
    const uint marginX=target.margin.x+1, marginY=target.margin.y+1, marginZ=target.margin.z+1;
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        const short3* const positionZ = positionData+z*XY;
        uint16* const targetZ = targetData+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const short3* const positionZY = positionZ+y*X;
            uint16* const targetZY = targetZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                const short3* const pos = positionZY+x;
                uint16* const skel = targetZY+x;
                if(pos[0]!=short3(x,y,z)) {
                    if(pos[-1]!=short3(x-1,y,z)) compare(skel,pos,x,y,z, -1,0,0, -1, minimalSqDiameter);
                    if(pos[-X]!=short3(x,y-1,z)) compare(skel,pos,x,y,z, 0,-1,0, -X, minimalSqDiameter);
                    if(pos[-XY]!=short3(x,y,z-1)) compare(skel,pos,x,y,z, 0,0,-1, -XY, minimalSqDiameter);
                }
            }
        }
    });
    target.margin.x = marginX, target.margin.y = marginY, target.margin.z = marginZ;
    target.maximum = maximum(target), target.squared=true;
    target.field = String("r"_); // Radius
}

/// Keeps only voxels on the medial axis of the pore space (integer medial axis skeleton ~ centers of maximal spheres)
class(Skeleton, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(uint16); }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override {
        integerMedialAxis(outputs[0],inputs[0], /*λ²=*/1); // 3 will remove artifacts due to discrete background but may also round corners
    }
};
