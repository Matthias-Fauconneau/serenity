#include "distance.h"
#include "time.h"
#include "simd.h"

// FIXME: try to factor these

// X
void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, Volume16& positionX, const Volume32& source, int X, int Y, int Z) {
    const uint32* const sourceData = source;
    uint32* const targetData = target;
    uint16* const xPositionData = positionX;
    clear(targetData, Z*Y*X); //FIXME: clear only margins
    const uint XY = X*Y;
    constexpr uint unroll = 8;
    int marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    assert_((Y-2*marginY)%unroll == 0);
    struct element { uint sd; int16 cx, x; };
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        uint16* const xPositionZ = xPositionData+z*X;
        for(int y=marginY; y<Y-marginY; y+=unroll) {
            const uint stackSize = X;
            element stacks[unroll*stackSize]; //64K
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
            for(int x=X-1; x>=0; x--) {
                uint32* const targetZYX = targetZY+x*XY;
                uint16* const xPositionZYX = xPositionZY+x*XY;
                for(uint dy=0; dy<unroll; dy++) {
                    int& i = stackIndices[dy];
                    memory<element> stack (stacks+dy*stackSize, stackSize);
                    if(x==stack[i].cx) i--;
                    assert_(i>=0);
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
void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, Volume16& positionX, Volume16& positionY, const Volume32& source, const Volume16& sourceX, int X, int Y, int Z) {
    const uint32* const sourceData = source;
    const uint16* const xSourceData = sourceX;
    uint32* const targetData = target;
    uint16* const xPositionData = positionX;
    uint16* const yPositionData = positionY;
    clear(targetData, Z*Y*X); //FIXME: clear only margins
    const uint XY = X*Y;
    constexpr uint unroll = 8;
    int marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    assert_((Y-2*marginY)%unroll == 0);
    struct element { uint sd; int16 cx, x; };
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*XY;
        const uint16* const xSourceZ = xSourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        uint16* const xPositionZ = xPositionData+z*X;
        uint16* const yPositionZ = yPositionData+z*X;
        for(int y=marginY; y<Y-marginY; y+=unroll) {
            const uint stackSize = X;
            element stacks[unroll*stackSize]; //64K
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
            for(int x=X-1; x>=0; x--) {
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
void perpendicularBisectorEuclideanDistanceTransform(Volume32& target, Volume16& positionX, Volume16& positionY, Volume16& positionZ, const Volume32& source, const Volume16& sourceX, const Volume16& sourceY, int X, int Y, int Z) {
    const uint32* const sourceData = source;
    const uint16* const xSourceData = sourceX;
    const uint16* const ySourceData = sourceY;
    uint32* const targetData = target;
    uint16* const xPositionData = positionX;
    uint16* const yPositionData = positionY;
    uint16* const zPositionData = positionZ;
    clear(targetData, Z*Y*X); //FIXME: clear only margins
    const uint XY = X*Y;
    constexpr uint unroll = 8;
    int marginX=source.marginX, marginY=source.marginY, marginZ=source.marginZ;
    assert_((Y-2*marginY)%unroll == 0);
    struct element { uint sd; int16 cx, x; };
    parallel(marginZ, Z-marginZ, [&](uint, uint z) {
        const uint32* const sourceZ = sourceData+z*XY;
        const uint16* const xSourceZ = xSourceData+z*XY;
        const uint16* const ySourceZ = ySourceData+z*XY;
        uint32* const targetZ = targetData+z*X;
        uint16* const xPositionZ = xPositionData+z*X;
        uint16* const yPositionZ = yPositionData+z*X;
        uint16* const zPositionZ = zPositionData+z*X;
        for(int y=marginY; y<Y-marginY; y+=unroll) {
            const uint stackSize = X;
            element stacks[unroll*stackSize]; //64K
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
            for(int x=X-1; x>=0; x--) {
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
                    int d = x * (x - 2*sx) + stack[i].sd;
                    targetZYX[dy] = d;
                    xPositionZYX[dy] = xSourceZY[dy*X + sx];
                    yPositionZYX[dy] = ySourceZY[dy*X + sx];
                    zPositionZYX[dy] = sx;
                }
            }
        }
    });
    target.squared=true;
    positionX.squared=false, positionX.maximum=X-1;
    positionY.squared=false, positionY.maximum=Y-1;
    positionZ.squared=false, positionZ.maximum=Z-1;
}
