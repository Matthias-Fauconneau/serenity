#include "distance.h"
#include "time.h"
#include "simd.h"

// FIXME: try to factor these

// X
void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, Volume16& positionX, const Volume32& source) {
    clearMargins(target);
    const int X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    int marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    const uint32* const sourceData = source;
    uint32* const targetData = target;
    uint16* const xPositionData = positionX;
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        uint16* const xPositionZ = xPositionData+z*X;
        constexpr uint unroll = 8;
        //assert(marginX && marginY && marginZ && (Y-2*marginY)%unroll == 0);
        for(int y=marginY; y<Y-marginY; y+=unroll) {
            const uint stackSize = X;
            struct element { uint sd; int16 cx, x; } stacks[unroll*stackSize];
            int stackIndices[unroll];
            for(uint dy=0; dy<unroll; dy++) {
                memory<element> stack (stacks+dy*stackSize, stackSize);
                const uint32* const sourceY = sourceZ+y*X+dy*X;
                int& i = stackIndices[dy]; i=-1;
                for(int x=marginX; x<X-marginX; x++) {
                    uint sd = sourceY[x];
                    if(sd < 0xFFFFFFFF) {
                        label:
                        if(i >= 0) {
                            int cx = sd > stack[i].sd ? (sd-stack[i].sd) / (2 * (x - stack[i].x)) : -2;
                            assert(cx>=-2 && cx<32768);
                            if(cx == stack[i].cx) stack[i]={sd,int16(cx),int16(x)};
                            else if(cx < stack[i].cx) { i--; goto label; }
                            else if(cx < X-marginX) stack[++i] = {sd,int16(cx),int16(x)};
                        } else stack[++i] = {sd,-1,int16(x)};
                    }
                }
            }
            uint32* const targetZY = targetZ+y;
            uint16* const xPositionZY = xPositionZ+y;
            for(int x=X-marginX-1; x>=marginX; x--) {
                uint32* const targetZYX = targetZY+x*XY;
                uint16* const xPositionZYX = xPositionZY+x*XY;
                for(uint dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    memory<element> stack (stacks+dy*stackSize, stackSize);
                    if(x==stack[i].cx) i--;
                    assert(i>=0);
                    int sx = stack[i].x;
                    int d = x * (x - 2*sx) + stack[i].sd;
                    targetZYX[dy] = (y+dy)*(y+dy) + d;
                    xPositionZYX[dy] = sx;
                }
            }
        }
    });
    target.squared=true;
    positionX.squared=false, positionX.maximum=X-1;
}

// Y
void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, Volume16& positionX, Volume16& positionY, const Volume32& source, const Volume16& sourceX) {
    clearMargins(target);
    const int X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    int marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    const uint32* const sourceData = source;
    const uint16* const xSourceData = sourceX;
    uint32* const targetData = target;
    uint16* const xPositionData = positionX;
    uint16* const yPositionData = positionY;
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*XY;
        const uint16* const xSourceZ = xSourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        uint16* const xPositionZ = xPositionData+z*X;
        uint16* const yPositionZ = yPositionData+z*X;
        constexpr uint unroll = 8;
        //assert(marginX && marginY && marginZ && (Y-2*marginY)%unroll == 0);
        for(int y=marginY; y<Y-marginY; y+=unroll) {
            const uint stackSize = X;
            struct element { uint sd; int16 cx, x; } stacks[unroll*stackSize];
            int stackIndices[unroll];
            for(uint dy=0; dy<unroll; dy++) {
                memory<element> stack (stacks+dy*stackSize, stackSize);
                const uint32* const sourceZY = sourceZ+y*X+dy*X;
                int& i = stackIndices[dy]; i=-1;
                for(int x=marginX; x<X-marginX; x++) {
                    uint sd = sourceZY[x];
                    if(sd < 0xFFFFFFFF) {
                        label:
                        if(i >= 0) {
                            int cx = sd > stack[i].sd ? (sd-stack[i].sd) / (2 * (x - stack[i].x)) : -2;
                            assert(cx>=-2 && cx<32768);
                            if(cx == stack[i].cx) stack[i]={sd,int16(cx),int16(x)};
                            else if(cx < stack[i].cx) { i--; goto label; }
                            else if(cx < X-marginX) stack[++i] = {sd,int16(cx),int16(x)};
                        } else stack[++i] = {sd,-1,int16(x)};
                    }
                }
            }
            const uint16* const xSourceZY = xSourceZ+y*X;
            uint32* const targetZY = targetZ+y;
            uint16* const xPositionZY = xPositionZ+y;
            uint16* const yPositionZY = yPositionZ+y;
            for(int x=X-marginX-1; x>=marginX; x--) {
                uint32* const targetZYX = targetZY+x*XY;
                uint16* const xPositionZYX = xPositionZY+x*XY;
                uint16* const yPositionZYX = yPositionZY+x*XY;
                for(uint dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    memory<element> stack (stacks+dy*stackSize, stackSize);
                    if(x==stack[i].cx) i--;
                    assert_(i>=0);
                    int sx = stack[i].x;
                    int d = x * (x - 2*sx) + stack[i].sd;
                    targetZYX[dy] = (y+dy)*(y+dy) + d;
                    xPositionZYX[dy] = xSourceZY[dy*X + sx];
                    yPositionZYX[dy] = sx;
                }
            }
        }
    });
    target.squared=true;
    positionX.squared=false, positionX.maximum=X-1;
    positionY.squared=false, positionY.maximum=Y-1;
}

// Z
void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, Volume16& positionX, Volume16& positionY, Volume16& positionZ, const Volume32& source, const Volume16& sourceX, const Volume16& sourceY) {
    clearMargins(target);
    const int X=source.x, Y=source.y, Z=source.z, XY=X*Y;
    int marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    const uint32* const sourceData = source;
    const uint16* const xSourceData = sourceX;
    const uint16* const ySourceData = sourceY;
    uint32* const targetData = target;
    uint16* const xPositionData = positionX;
    uint16* const yPositionData = positionY;
    uint16* const zPositionData = positionZ;
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*XY;
        const uint16* const xSourceZ = xSourceData+z*XY;
        const uint16* const ySourceZ = ySourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        uint16* const xPositionZ = xPositionData+z*X;
        uint16* const yPositionZ = yPositionData+z*X;
        uint16* const zPositionZ = zPositionData+z*X;
        constexpr uint unroll = 8;
        //assert(marginX && marginY && marginZ && (Y-2*marginY)%unroll == 0);
        for(int y=marginY; y<Y-marginY; y+=unroll) {
            const uint stackSize = X;
            struct element { uint sd; int16 cx, x; } stacks[unroll*stackSize];
            int stackIndices[unroll];
            for(uint dy=0; dy<unroll; dy++) {
                memory<element> stack (stacks+dy*stackSize, stackSize);
                const uint32* const sourceZY = sourceZ+y*X+dy*X;
                int& i = stackIndices[dy]; i=-1;
                for(int x=marginX; x<X-marginX; x++) {
                    uint sd = sourceZY[x];
                    if(sd < 0xFFFFFFFF) {
                        label:
                        if(i >= 0) {
                            int cx = sd > stack[i].sd ? (sd-stack[i].sd) / (2 * (x - stack[i].x)) : -2;
                            assert(cx>=-2 && cx<32768);
                            if(cx == stack[i].cx) stack[i]={sd,int16(cx),int16(x)};
                            else if(cx < stack[i].cx) { i--; goto label; }
                            else if(cx < X-marginX) stack[++i] = {sd,int16(cx),int16(x)};
                        } else stack[++i] = {sd,-1,int16(x)};
                    }
                }
            }
            const uint16* const xSourceZY = xSourceZ+y*X;
            const uint16* const ySourceZY = ySourceZ+y*X;
            uint32* const targetZY = targetZ+y;
            uint16* const xPositionZY = xPositionZ+y;
            uint16* const yPositionZY = yPositionZ+y;
            uint16* const zPositionZY = zPositionZ+y;
            for(int x=X-1-marginX; x>=marginX; x--) {
                uint32* const targetZYX = targetZY+x*XY;
                uint16* const xPositionZYX = xPositionZY+x*XY;
                uint16* const yPositionZYX = yPositionZY+x*XY;
                uint16* const zPositionZYX = zPositionZY+x*XY;
                for(uint dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    memory<element> stack (stacks+dy*stackSize, stackSize);
                    if(x==stack[i].cx) i--;
                    assert_(i>=0);
                    int sx = stack[i].x;
                    int sqRadius = x * (x - 2*sx) + stack[i].sd;
                    targetZYX[dy] = sqRadius;
                    xPositionZYX[dy] = xSourceZY[dy*X + sx];
                    yPositionZYX[dy] = ySourceZY[dy*X + sx];
                    zPositionZYX[dy] = sx;
                }
            }
        }
    });
#if 0 // Validation
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        for(int y: range(marginY, Y-marginY)) {
            for(int x: range(marginX, X-marginX)) {
                int sqRadius = targetData[z*XY+y*X+x];
                //if(!sqRadius) assert(sourceData[y*XY+x*X+z]==0xFFFFFFFF);
                if(sqRadius > 1 && sqRadius<=3) {
                    int radius = ceil(sqrt(sqRadius));
                    const uint32* const sourceO = sourceData+y*XY+x*X+z;
                    for(int dz=-radius; dz<=radius; dz++) {
                        const uint32* const sourceZ = sourceO+dz;
                        for(int dy=-radius; dy<=radius; dy++) {
                            const uint32* const sourceZY = sourceZ+dy*XY;
                            for(int dx=-radius; dx<=radius; dx++) {
                                if(dx*dx+dy*dy+dz*dz<sqRadius) {
                                    int distancey = sourceZY[dx*X];
                                    int r = targetData[(int(z)+dz)*XY+(y+dy)*X+(x+dx)];
                                    assert_(distancey!=(int(z)+dz)*(int(z)+dz), marginX, marginY, marginZ, x,y,z, dx,dy,dz,dx*dx+dy*dy+dz*dz,sqRadius);
                                    assert_(r, marginX, marginY, marginZ, x,y,z, dx,dy,dz,dx*dx+dy*dy+dz*dz,sqRadius);
                                }
                            }
                        }
                    }
                }
            }
        }
    });
#endif
    target.squared=true;
    positionX.squared=false, positionX.maximum=X-1;
    positionY.squared=false, positionY.maximum=Y-1;
    positionZ.squared=false, positionZ.maximum=Z-1;
}
